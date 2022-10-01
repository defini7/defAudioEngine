#pragma once

#pragma region license
/***
*	BSD 3-Clause License
	Copyright (c) 2021, 2022 Alex
	All rights reserved.
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	1. Redistributions of source code must retain the above copyright notice, this
	   list of conditions and the following disclaimer.
	2. Redistributions in binary form must reproduce the above copyright notice,
	   this list of conditions and the following disclaimer in the documentation
	   and/or other materials provided with the distribution.
	3. Neither the name of the copyright holder nor the names of its
	   contributors may be used to endorse or promote products derived from
	   this software without specific prior written permission.
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***/
#pragma endregion

#pragma region sample
/**
	#include <iostream>

	#include "defAudioEngine.h"

	int main()
	{
		// Creating instance of main class
		def::AudioEngine audio;

		// Loading audio in queue and getting it's id
		int nSample1 = audio.LoadAudioSample("p1elim.wav");

		// Playing sample
		audio.PlaySample(nSample1, true);

		// Because audio engine runs on different thread 
		// we need to check if there are any active (currently playing) samples
		while (audio.listActiveSamples.size() > 0) {}

		// "Tell" to the engine that we want to stop it
		audio.DestroyAudio();

		return 0;
	}
**/
#pragma endregion

#if !defined(DEF_PLATFORM_CUSTOM)
	#if defined(_WIN32)
		#define DEF_PLATFORM_WINMM
	#elif defined(__linux__) || defined(__apple__)
		#define DEF_PLATFORM_SDL2
	#endif
#endif

#if defined(DEF_PLATFORM_WINMM)
	#include <Windows.h>
	#pragma comment(lib, "winmm.lib")

	#include <atomic>
	#include <thread>
	#include <mutex>
	#include <condition_variable>
#endif

#if defined(DEF_PLATFORM_SDL2)
	#include <iostream>

	#if defined(__linux__) || defined(__apple__)
		#include <SDL2/SDL.h>
	#else
		#include <SDL.h>
	#endif

	#if defined(_WIN32) && !defined(__MINGW32__)
		#pragma comment(lib, "SDL2.lib")
		#pragma comment(lib, "SDL2main.lib")
	#else
	/*
	* Link your libraries
	* with program
	*/
	#endif

	#if defined(SDL_MAIN_NEEDED) || !defined(SDL_MAIN_AVAILABLE)
		#if defined(__linux__) || defined(__apple__)
			#define main() main(int argc, char** argv)
		#else
			#define main() SDL_main(int argc, char** argv)
		#endif
	#else
		#undef main
		#define main() SDL_main(int argc, char** argv)
	#endif
#endif

#include <list>
#include <vector>
#include <string>

namespace def
{
	class AudioEngine
	{
	public:
		AudioEngine()
		{
#if defined(DEF_PLATFORM_SDL2)
			if (SDL_Init(SDL_INIT_AUDIO) < 0)
				std::cerr << "Can't initialize audio!\n";
			else
#endif
				m_bAudioThreadActive = CreateAudio();
		}

		struct AudioSample
		{
		public:
			AudioSample() = default;

			AudioSample(std::string sWavFile, AudioEngine& ae)
			{
#if defined(DEF_PLATFORM_WINMM)
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
#elif defined(DEF_PLATFORM_SDL2)
				uint8_t* wavData;
				SDL_AudioSpec fileSpec;
				uint32_t streamLen = 0;

				if (!SDL_LoadWAV(sWavFile.c_str(), &fileSpec, (uint8_t**)&wavData, &streamLen))
				{
					std::cerr << "SDL: couldn't load audio file: " << SDL_GetError() << '\n';
					bSampleValid = false;
					return;
				}

				SDL_AudioCVT cvt;
				if (SDL_BuildAudioCVT(&cvt, fileSpec.format, fileSpec.channels, fileSpec.freq, ae.m_sdlSampleSpec.format, ae.m_sdlSampleSpec.channels, ae.m_sdlSampleSpec.freq) < 0)
				{
					std::cerr << "SDL: failed to build cvt: " << SDL_GetError() << '\n';
					bSampleValid = false;
					SDL_FreeWAV(wavData);
					return;
				}

				cvt.buf = (uint8_t*)malloc(streamLen * cvt.len_mult);
				cvt.len = streamLen;

				memcpy(cvt.buf, wavData, streamLen);

				SDL_FreeWAV(wavData);

				if (SDL_ConvertAudio(&cvt) < 0)
				{
					std::cerr << "SDL: failed to convert audio!: " << SDL_GetError() << '\n';
					bSampleValid = false;
					SDL_free(cvt.buf);
					return;
				}

				fSample = (float*)cvt.buf;
				nSamples = cvt.len_cvt / sizeof(float) / ae.m_sdlSpec.channels;
				bSampleValid = true;

				SDL_free(cvt.buf);
#endif
			}

#if defined(DEF_PLATFORM_SDL2)
			~AudioSample()
			{
				SDL_FreeWAV((uint8_t*)fSample);
			}
#endif

#if defined(DEF_PLATFORM_WINMM)
			WAVEFORMATEX wavHeader;
#endif
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
			if (!m_bAudioThreadActive)
				return -1;

			AudioSample a(sWavFile, *this);
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
#if defined(DEF_PLATFORM_WINMM)
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

			m_bAudioThreadActive = true;
			tAudioThread = std::thread(&AudioEngine::AudioThread, this);

			std::unique_lock<std::mutex> lm(muxBlockNotZero);
			cvBlockNotZero.notify_one();
#elif defined(DEF_PLATFORM_SDL2)
			SDL_AudioSpec wanted;

			SDL_zero(wanted);

			wanted.channels = channels;
			wanted.format = AUDIO_S16;
			wanted.freq = sample_rate;
			wanted.samples = blocks;
			wanted.userdata = this;
			wanted.callback = forwardCallback;

			SDL_zero(m_sdlSampleSpec);

			m_sdlSampleSpec.channels = channels;
			m_sdlSampleSpec.format = AUDIO_F32;
			m_sdlSampleSpec.freq = sample_rate;
			m_sdlSampleSpec.userdata = this;

			m_sdlAudioDeviceID = SDL_OpenAudioDevice(0, 0, &wanted, &m_sdlSpec, 0);

			if (!m_sdlAudioDeviceID)
			{
				std::cout << "Failed to open audio device!\n" << SDL_GetError() << '\n';
				return false;
			}

			SDL_PauseAudioDevice(m_sdlAudioDeviceID, 0);
#endif

			return true;
		}

		bool DestroyAudio()
		{
			m_bAudioThreadActive = false;

#if defined(DEF_PLATFORM_WINMM)
			if (tAudioThread.joinable())
				tAudioThread.join();
#elif defined(DEF_PLATFORM_SDL2)
			SDL_CloseAudioDevice(m_sdlAudioDeviceID);
#endif

			return false;
		}

#if defined(DEF_PLATFORM_WINMM)
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
#elif defined(DEF_PLATFORM_SDL2)
		static void forwardCallback(void* userdata, uint8_t* byteStream, int len)
		{
			static_cast<AudioEngine*>(userdata)->AudioThread(userdata, byteStream, len);
		}
#endif

#if defined(DEF_PLATFORM_WINMM)
		void AudioThread()
		{
			float fTimeStep;
			short nMaxSample, nPreviousSample;

			fTimeStep = 1.0f / (float)nSampleRate;

			nMaxSample = (short)pow(2, (sizeof(short) * 8) - 1) - 1;

			while (m_bAudioThreadActive)
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
#elif defined(DEF_PLATFORM_SDL2)
		void AudioThread(void* userdata, uint8_t* byteStream, int len)
		{
			m_fGlobalTime = 0.0f;

			float fTimeStep = 1.0f / (float)m_sdlSpec.freq;
			float fMaxSample = float(std::pow(2, (sizeof(short) * 8) - 1) - 1.0);
			short nPreviousSample = 0;

			memset(byteStream, 0, len);
			
			int16_t* buf = (int16_t*)byteStream;

			auto clip = [](float fSample, float fMax)
			{
				return (fSample >= 0.0f) ? std::fmin(fSample, fMax) : std::fmax(fSample, -fMax);
			};

			uint32_t i = 0;
			for (unsigned int n = 0; n < len / sizeof(int16_t); n += m_sdlSpec.channels)
			{
				for (unsigned int c = 0; c < m_sdlSpec.channels; c++)
				{
					buf[i] = int16_t(clip(GetMixerOutput(c, fTimeStep * i, fTimeStep), 1.0f) * fMaxSample);
					i++;
				}

				m_fGlobalTime += fTimeStep;
			}
		}
#endif

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
#if defined(DEF_PLATFORM_WINMM)
				s.nSamplePosition += (long)((float)vecAudioSamples[s.nAudioSampleID - 1].wavHeader.nSamplesPerSec * fTimeStep);
#elif defined(DEF_PLATFORM_SDL2)
				s.nSamplePosition += (long)((float)m_sdlSpec.freq * fTimeStep);
#endif
				if (s.nSamplePosition < vecAudioSamples[s.nAudioSampleID - 1].nSamples)
				{
#if defined(DEF_PLATFORM_WINMM)
					fMixerSample += vecAudioSamples[s.nAudioSampleID - 1].fSample[(s.nSamplePosition * vecAudioSamples[s.nAudioSampleID - 1].nChannels) + nChannel];
#elif defined(DEF_PLATFORM_SDL2)
					fMixerSample += vecAudioSamples[s.nAudioSampleID - 1].fSample[(s.nSamplePosition * m_sdlSpec.channels) + nChannel];
#endif
				}
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
#if defined(DEF_PLATFORM_WINMM)
		short* pBlockMemory = nullptr;
		HWAVEOUT hwDevice = nullptr;

		unsigned int nSampleRate;
		unsigned int nChannels;
		unsigned int nBlockCount;
		unsigned int nBlockSamples;
		unsigned int nBlockCurrent;
		WAVEHDR* pWaveHeaders;

		std::thread tAudioThread;
		std::atomic<bool> m_bAudioThreadActive;
		std::atomic<unsigned int> nBlockFree;
		std::condition_variable cvBlockNotZero;
		std::mutex muxBlockNotZero;
		std::atomic<float> fGlobalTime = 0.0f;
#elif defined(DEF_PLATFORM_SDL2)
	public:
		SDL_AudioSpec m_sdlSpec, m_sdlSampleSpec;
		SDL_AudioDeviceID m_sdlAudioDeviceID;

	private:
		std::atomic<float> m_fGlobalTime = 0.0f;
		std::atomic<bool> m_bAudioThreadActive = false;
#endif
	};
}

