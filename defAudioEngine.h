#pragma once

#include <Windows.h>
#include <list>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "winmm.lib")

namespace def
{
	class AudioEngine
	{
	public:
		AudioEngine()
		{
			bAudioThreadActive = CreateAudio();

			fGlobalTime = 0.0f;
		}

		struct AudioSample
		{
		public:
			AudioSample()
			{

			}

			AudioSample(std::string sWavFile)
			{
				FILE* f = nullptr;
				fopen_s(&f, sWavFile.c_str(), "rb");
				if (f == nullptr)
					return;

				char dump[4];
				fread(&dump, sizeof(char), 4, f);
				if (strncmp(dump, "RIFF", 4) != 0) return;
				fread(&dump, sizeof(char), 4, f); // Not used
				fread(&dump, sizeof(char), 4, f);
				if (strncmp(dump, "WAVE", 4) != 0) return;

				// Read Wave description chunk
				fread(&dump, sizeof(char), 4, f); // Read "fmt "
				fread(&dump, sizeof(char), 4, f); // Not used
				fread(&wavHeader, sizeof(WAVEFORMATEX) - 2, 1, f); // Read Wave Format Structure chunk
				// Note the -2, because the structure has 2 bytes to indicate its own size
				// which are not in the wav file

				// Check if it's 16-bit WAVE file @ 44100Hz
				if (wavHeader.wBitsPerSample != 16 || wavHeader.nSamplesPerSec != 44100)
				{
					fclose(f);
					return;
				}

				// Search for audio data chunk
				long nChunksize = 0;
				fread(&dump, sizeof(char), 4, f); // Read chunk header
				fread(&nChunksize, sizeof(long), 1, f); // Read chunk size

				while (strncmp(dump, "data", 4) != 0)
				{
					// Not audio data, so just skip it
					fseek(f, nChunksize, SEEK_CUR);
					fread(&dump, sizeof(char), 4, f);
					fread(&nChunksize, sizeof(long), 1, f);
				}

				nSamples = nChunksize / (wavHeader.nChannels * (wavHeader.wBitsPerSample >> 3));
				nChannels = wavHeader.nChannels;

				fSample = new float[nSamples * nChannels];
				float* pSample = fSample;

				for (long i = 0; i < nSamples; i++)
				{
					for (int c = 0; c < nChannels; c++)
					{
						short s = 0;
						fread(&s, sizeof(short), 1, f);
						*pSample = (float)s / (float)(MAXSHORT);
						pSample++;
					}
				}

				fclose(f);
				bSampleValid = true;
			}

			WAVEFORMATEX wavHeader;
			float* fSample = nullptr;
			long nSamples = 0;
			int nChannels = 0;
			bool bSampleValid = false;
		};

		std::vector<AudioSample> vecAudioSamples;

		struct sCurrentlyPlayingSample
		{
			int nAudioSampleID = 0;
			long nSamplePosition = 0;
			bool bFinished = false;
			bool bLoop = false;
		};
		std::list<sCurrentlyPlayingSample> listActiveSamples;

		// 16-bit WAVE file @ 44100Hz ONLY
		unsigned int LoadAudioSample(std::string sWavFile)
		{
			if (!bAudioThreadActive)
				return -1;

			AudioSample a(sWavFile);
			if (a.bSampleValid)
			{
				vecAudioSamples.push_back(a);
				return vecAudioSamples.size();
			}
			else
				return -1;
		}

		void PlaySample(int id, bool bLoop = false)
		{
			sCurrentlyPlayingSample a;
			a.nAudioSampleID = id;
			a.nSamplePosition = 0;
			a.bFinished = false;
			a.bLoop = bLoop;
			listActiveSamples.push_back(a);
		}

		void StopSample(int id)
		{
			listActiveSamples.remove_if([id](sCurrentlyPlayingSample& s) { return s.nAudioSampleID == id; });
		}

		bool CreateAudio(unsigned int sample_rate = 44100, unsigned int channels = 1,
			unsigned int blocks = 8, unsigned int block_samples = 512)
		{
			nSampleRate = sample_rate;
			nChannels = channels;
			nBlockCount = blocks;
			nBlockSamples = block_samples;
			nBlockFree = nBlockCount;
			nBlockCurrent = 0;
			pBlockMemory = nullptr;
			pWaveHeaders = nullptr;

			WAVEFORMATEX waveFormat;
			waveFormat.wFormatTag = WAVE_FORMAT_PCM;
			waveFormat.nSamplesPerSec = nSampleRate;
			waveFormat.wBitsPerSample = sizeof(short) * 8;
			waveFormat.nChannels = nChannels;
			waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
			waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
			waveFormat.cbSize = 0;

			if (waveOutOpen(&hwDevice, WAVE_MAPPER, &waveFormat, (DWORD_PTR)waveOutProcWrap, (DWORD_PTR)this, CALLBACK_FUNCTION) != S_OK)
				return DestroyAudio();

			pBlockMemory = new short[nBlockCount * nBlockSamples];
			if (pBlockMemory == nullptr)
				return DestroyAudio();

			ZeroMemory(pBlockMemory, sizeof(short) * nBlockCount * nBlockSamples);

			pWaveHeaders = new WAVEHDR[nBlockCount];
			if (pWaveHeaders == nullptr)
				return DestroyAudio();

			ZeroMemory(pWaveHeaders, sizeof(WAVEHDR) * nBlockCount);

			for (unsigned int n = 0; n < nBlockCount; n++)
			{
				pWaveHeaders[n].dwBufferLength = nBlockSamples * sizeof(short);
				pWaveHeaders[n].lpData = (LPSTR)(pBlockMemory + (n * nBlockSamples));
			}

			bAudioThreadActive = true;
			tAudioThread = std::thread(&AudioEngine::AudioThread, this);

			std::unique_lock<std::mutex> lm(muxBlockNotZero);
			cvBlockNotZero.notify_one();

			return true;
		}

		bool DestroyAudio()
		{
			bAudioThreadActive = false;

			if (tAudioThread.joinable())
				tAudioThread.join();

			return false;
		}

		void waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwParam1, DWORD dwParam2)
		{
			if (uMsg != WOM_DONE) return;

			nBlockFree++;

			std::unique_lock<std::mutex> lm(muxBlockNotZero);
			cvBlockNotZero.notify_one();
		}

		static void CALLBACK waveOutProcWrap(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
		{
			((AudioEngine*)dwInstance)->waveOutProc(hWaveOut, uMsg, dwParam1, dwParam2);
		}

		void AudioThread()
		{
			float fTimeStep;
			short nMaxSample, nPreviousSample;

			fTimeStep = 1.0f / (float)nSampleRate;

			nMaxSample = (short)pow(2, (sizeof(short) * 8) - 1) - 1;

			while (bAudioThreadActive)
			{
				if (nBlockFree == 0)
				{
					std::unique_lock<std::mutex> lm(muxBlockNotZero);
					while (nBlockFree == 0)
						cvBlockNotZero.wait(lm);
				}

				nBlockFree--;

				if (pWaveHeaders[nBlockCurrent].dwFlags & WHDR_PREPARED)
					waveOutUnprepareHeader(hwDevice, &pWaveHeaders[nBlockCurrent], sizeof(WAVEHDR));

				int nNewSample, nCurrentBlock;

				nCurrentBlock = nBlockCurrent * nBlockSamples;

				auto clip = [](float fSample, float fMax)
				{
					if (fSample >= 0.0)
						return fmin(fSample, fMax);
					else
						return fmax(fSample, -fMax);
				};

				for (unsigned int n = 0; n < nBlockSamples; n += nChannels)
				{
					for (unsigned int c = 0; c < nChannels; c++)
					{
						nNewSample = (short)(clip(GetMixerOutput(c, fGlobalTime, fTimeStep), 1.0f) * (float)nMaxSample);
						pBlockMemory[nCurrentBlock + n + c] = nNewSample;
						nPreviousSample = nNewSample;
					}

					fGlobalTime += fTimeStep;
				}

				waveOutPrepareHeader(hwDevice, &pWaveHeaders[nBlockCurrent], sizeof(WAVEHDR));
				waveOutWrite(hwDevice, &pWaveHeaders[nBlockCurrent], sizeof(WAVEHDR));

				nBlockCurrent++;
				nBlockCurrent %= nBlockCount;
			}
		}

		// Overridden by user if they want to generate sound in real-time
		virtual float OnUserSoundSample(int nChannel, float fGlobalTime, float fTimeStep)
		{
			return 0.0f;
		}

		// Overriden by user if they want to manipulate the sound before it is played
		virtual float OnUserSoundFilter(int nChannel, float fGlobalTime, float fSample)
		{
			return fSample;
		}

		float GetMixerOutput(int nChannel, float fGlobalTime, float fTimeStep)
		{
			float fMixerSample = 0.0f;

			for (auto& s : listActiveSamples)
			{
				s.nSamplePosition += (long)((float)vecAudioSamples[s.nAudioSampleID - 1].wavHeader.nSamplesPerSec * fTimeStep);

				if (s.nSamplePosition < vecAudioSamples[s.nAudioSampleID - 1].nSamples)
					fMixerSample += vecAudioSamples[s.nAudioSampleID - 1].fSample[(s.nSamplePosition * vecAudioSamples[s.nAudioSampleID - 1].nChannels) + nChannel];
				else
					s.bFinished = true;
			}

			listActiveSamples.remove_if([&](const sCurrentlyPlayingSample& s) 
				{
					if (s.bFinished && s.bLoop)
						PlaySample(s.nAudioSampleID, true);

					return s.bFinished;
				});

			fMixerSample += OnUserSoundSample(nChannel, fGlobalTime, fTimeStep);

			return OnUserSoundFilter(nChannel, fGlobalTime, fMixerSample);
		}

	private:
		short* pBlockMemory = nullptr;
		HWAVEOUT hwDevice = nullptr;

		unsigned int nSampleRate;
		unsigned int nChannels;
		unsigned int nBlockCount;
		unsigned int nBlockSamples;
		unsigned int nBlockCurrent;
		WAVEHDR* pWaveHeaders;

		std::thread tAudioThread;
		std::atomic<bool> bAudioThreadActive;
		std::atomic<unsigned int> nBlockFree;
		std::condition_variable cvBlockNotZero;
		std::mutex muxBlockNotZero;
		std::atomic<float> fGlobalTime;

	};
}
