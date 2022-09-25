#include <iostream>

#include "defAudioEngine.h"

int main()
{
	Audio audio;

	int nSample1 = audio.LoadAudioSample("p1elim.wav");

	audio.PlaySample(nSample1);

	while (audio.listActiveSamples.size() > 0) {}

	audio.DestroyAudio();

	return 0;
}
