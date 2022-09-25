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
	Example audio;

	int nSample1 = audio.LoadAudioSample("p1elim.wav");

	audio.PlaySample(nSample1);

	while (audio.listActiveSamples.size() > 0) {}

	audio.DestroyAudio();

	return 0;
}
