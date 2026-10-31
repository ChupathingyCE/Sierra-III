/* musicenc: the music path's encoder (bake/lib/soundpack.py --music-stream).
Raw 16-bit little-endian PCM in, Vorbis packets out, without Ogg pages: the
game's music reader stores packets bare, so the container would only be
stripped again.

    musicenc RATE CHANNELS QUALITY IN.raw OUT.pkt

OUT.pkt is a run of records, each a 4-byte little-endian length, the packet's
8-byte little-endian granule position (the frame its decoded audio ends on;
-1 for the three header packets) and the packet's bytes. The headers come
first, in order: identification, comment (empty), setup.

The encode is deterministic: libvorbis takes no clock or random input, so the
same PCM gives the same packets on every machine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisenc.h>

static void put_packet(FILE *out, const ogg_packet *packet, int header)
{
	unsigned char head[12];
	unsigned long bytes = (unsigned long)packet->bytes;
	long long granule = header ? -1 : (long long)packet->granulepos;
	int k;

	for (k = 0; k < 4; k++)
		head[k] = (unsigned char)(bytes >> (8 * k));
	for (k = 0; k < 8; k++)
		head[4 + k] = (unsigned char)((unsigned long long)granule >> (8 * k));
	if (fwrite(head, 1, sizeof head, out) != sizeof head ||
		fwrite(packet->packet, 1, bytes, out) != bytes)
	{
		fprintf(stderr, "musicenc: writing failed\n");
		exit(1);
	}
}

int main(int argc, char **argv)
{
	vorbis_info info;
	vorbis_comment comment;
	vorbis_dsp_state dsp;
	vorbis_block block;
	ogg_packet identification, comments, setup, packet;
	FILE *in, *out;
	long rate;
	int channels, done = 0;
	float quality;

	if (argc != 6)
	{
		fprintf(stderr, "usage: musicenc RATE CHANNELS QUALITY IN.raw OUT.pkt\n");
		return 2;
	}
	rate = strtol(argv[1], NULL, 10);
	channels = (int)strtol(argv[2], NULL, 10);
	quality = (float)strtod(argv[3], NULL);
	if (rate <= 0 || channels < 1 || channels > 2 || quality < -0.1f || quality > 1.0f)
	{
		fprintf(stderr, "musicenc: RATE > 0, CHANNELS 1 or 2, QUALITY -0.1 to 1.0\n");
		return 2;
	}
	in = fopen(argv[4], "rb");
	out = fopen(argv[5], "wb");
	if (!in || !out)
	{
		fprintf(stderr, "musicenc: can't open %s\n", in ? argv[5] : argv[4]);
		return 1;
	}

	vorbis_info_init(&info);
	if (vorbis_encode_init_vbr(&info, channels, rate, quality) != 0)
	{
		fprintf(stderr, "musicenc: libvorbis refused %ld Hz, %d channels, quality %.2f\n", rate, channels, quality);
		return 1;
	}
	vorbis_comment_init(&comment);
	vorbis_analysis_init(&dsp, &info);
	vorbis_block_init(&dsp, &block);
	vorbis_analysis_headerout(&dsp, &comment, &identification, &comments, &setup);
	put_packet(out, &identification, 1);
	put_packet(out, &comments, 1);
	put_packet(out, &setup, 1);

	while (!done)
	{
		enum { FRAMES = 1024 };
		short pcm[FRAMES * 2];
		size_t frames = fread(pcm, sizeof(short) * (size_t)channels, FRAMES, in);

		if (frames == 0)
		{
			/* the end: libvorbis flushes what it holds and marks the last
			packet's granule as the exact frame count */
			vorbis_analysis_wrote(&dsp, 0);
			done = 1;
		}
		else
		{
			float **buffer = vorbis_analysis_buffer(&dsp, (int)frames);
			size_t f;
			int c;

			for (f = 0; f < frames; f++)
			{
				for (c = 0; c < channels; c++)
				{
					const unsigned char *b = (const unsigned char *)&pcm[f * (size_t)channels + (size_t)c];
					short s = (short)(b[0] | (b[1] << 8)); /* little-endian on every host */
					buffer[c][f] = s / 32768.f;
				}
			}
			vorbis_analysis_wrote(&dsp, (int)frames);
		}
		while (vorbis_analysis_blockout(&dsp, &block) == 1)
		{
			vorbis_analysis(&block, NULL);
			vorbis_bitrate_addblock(&block);
			while (vorbis_bitrate_flushpacket(&dsp, &packet))
				put_packet(out, &packet, 0);
		}
	}

	vorbis_block_clear(&block);
	vorbis_dsp_clear(&dsp);
	vorbis_comment_clear(&comment);
	vorbis_info_clear(&info);
	if (fclose(out) != 0)
	{
		fprintf(stderr, "musicenc: writing failed\n");
		return 1;
	}
	fclose(in);
	return 0;
}
