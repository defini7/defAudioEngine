#include <iostream>

#include "defAudioEngine.h"

class Example : public def::AudioEngine
{
public:
	Example()
	{

	}

private:
	// Overridden by user if they want to generate sound in real-time
	virtual float OnUserSoundSample(int nChannel, float fGlobalTime, float fTimeStep)
	{
		return 0.0f;
	}

	// Overriden by user if they want to manipulate the sound before it is played
	virtual float OnUserSoundFilter(int nChannel, float fGlobalTime, float fSample)
	{
		return sinf(fSample) * fGlobalTime;
	}

};

int main()
{
	// Creating instance of abstract class
	Example audio;

	// Loading audio in queue and getting it's id
	int nSample1 = audio.LoadAudioSample("p1elim.wav");

	// Playing sample
	audio.PlaySample(nSample1);

	// Because audio engine runs on different thread 
	// we need to check if there are any active (currently playing) samples
	while (audio.listActiveSamples.size() > 0) {}

	// "Tell" to the engine that we want to stop it
	audio.DestroyAudio();

	return 0;
}
