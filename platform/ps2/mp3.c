#include <stdio.h>
#include <string.h>

#include <kernel.h>

#include <mad.h>

#include "../../pico/pico_int.h"
#include "../../pico/sound/mix.h"
#include "../common/lprintf.h"

int mp3_last_error = 0;

static unsigned char initialized = 0;

// MPEG-1, layer 3
static const unsigned short int bitrates[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
//static const unsigned int samplerates[] = { 44100, 48000, 32000, 0 };

//Multiply the buffer sizes by 2 since stereo streams have twice the number of samples of mono streams.
#define NUM_BLOCKS	4
#define IN_BUFFER_SIZE ((1024*NUM_BLOCKS)*2)
#define SAMPLES_PER_FRAME	1152	//The (maximum) number of samples per MP3 frame.
#define OUT_BUFFER_SIZE	((SAMPLES_PER_FRAME*NUM_BLOCKS)*2)	//Don't set this below 2*SAMPLES_PER_FRAME*channels, since this system will ensure that there will be enough space to insert a set of decoded stereo samples into the ring buffer before actually decoding the MP3. If it's set too low, it'll cause no audio to be actually output.

static struct mad_stream Mp3Stream;
static struct mad_frame Mp3Frame;
static struct mad_synth Mp3Synth;

static unsigned short int OutputBufferLevel, buffer_rd_ptr, LeftOverData_level;//LeftOverData_level indicates whether there is leftover data stored within the MP3 input buffer (Leftover from a partial frame read, which occurs when part of a frame exists at the end of the input buffer).

static unsigned char mp3_src_buffer[IN_BUFFER_SIZE+MAD_BUFFER_GUARD] __attribute__((aligned(64)));
static void *GuardPtr;	//Pointer to the guard byte region, for libmad.
static short int mp3_mix_buffer[OUT_BUFFER_SIZE] __attribute__((aligned(64)));

static const char *mp3_fname = NULL;
static FILE *mp3_handle = NULL;
static int mp3_src_pos = 0, mp3_src_size = 0;

/*
 * The following utility routine performs simple rounding, clipping, and
 * scaling of MAD's high-resolution samples down to 16 bits. It does not
 * perform any dithering or noise shaping, which would be recommended to
 * obtain any exceptional audio quality. It is therefore not recommended to
 * use this routine if high-quality output is desired.
 */
static inline int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static void DeinitMAD(void){
	mad_synth_finish(&Mp3Synth);
	mad_frame_finish(&Mp3Frame);
	mad_stream_finish(&Mp3Stream);
}

int mp3_init(void)
{
	int ret;

	/* audiocodec init */
	mad_stream_init(&Mp3Stream);
	mad_frame_init(&Mp3Frame);
	mad_synth_init(&Mp3Synth);
	//goto fail;

	mp3_last_error = 0;
	initialized = 1;
	return 0;

	DeinitMAD();
//fail:
	mp3_last_error = ret;
	initialized = 0;
	return 1;
}

void mp3_deinit(void)
{
	lprintf("mp3_deinit, initialized=%i\n", initialized);

	if (!initialized) return;

	if (mp3_handle != NULL) fclose(mp3_handle);
	mp3_handle = NULL;
	mp3_fname = NULL;

	DeinitMAD();
	initialized = 0;
}

static inline int Mp3Output(struct mad_pcm *pcm, short int *output, int maxsamples)
{
	unsigned int nsamples;
	int NumSamplesWritten;
	mad_fixed_t const *left_ch, *right_ch;
	short int *ptr;

	if(pcm->length*pcm->channels<=maxsamples){
		NumSamplesWritten = nsamples = pcm->length;
		left_ch   = pcm->samples[0];
		right_ch  = pcm->samples[1];

		/* output sample(s) in 16-bit signed little-endian PCM */
		ptr=output;
		while (nsamples--) {
			*ptr++ = scale(*left_ch++);
			*ptr++ = scale(*right_ch++);
		}
	}
	else NumSamplesWritten=0;

	return(NumSamplesWritten * pcm->channels);
}

static int ConvertMP3(short int *output, unsigned int samples){
	int TotalConvertedSamples, bytes_read, ret, NumMP3SamplesConverted;

	for(TotalConvertedSamples=0; (TotalConvertedSamples<samples) && (samples-TotalConvertedSamples>=SAMPLES_PER_FRAME*2);){	//Ensure that there will be enough space within the ring buffer for inserting the decoded samples into (Assume that it's stereo; mono tracks will require less space).
		/*	Fill the input buffer when this is the first read, and when more data is required.
			***Unused data will be left in the read buffer, since losing them would leave the MP3 stream in an undefined position (will lead to lots of errors whenever the conversion process starts up again).***
				I wasted a LOT of time wondering why this library wouldn't work right because of that reason. :(
				The stream records the next sample within the read buffer that hasn't been converted, so don't interfere with that (By either resetting the stream or erasing the data stored within the read buffer, unless the MP3 is to be seeked or changed).
			*/
		if(Mp3Stream.buffer==NULL || Mp3Stream.error==MAD_ERROR_BUFLEN){
			if(Mp3Stream.next_frame!=NULL){	//When there's a partial frame at the end of the buffer.
				LeftOverData_level=Mp3Stream.bufend-Mp3Stream.next_frame;
				memmove(mp3_src_buffer, Mp3Stream.next_frame, LeftOverData_level);
			}

			if((bytes_read = fread(mp3_src_buffer+LeftOverData_level, 1, IN_BUFFER_SIZE-LeftOverData_level, mp3_handle))!=IN_BUFFER_SIZE-LeftOverData_level){
				//EOF reached.
				GuardPtr=&mp3_src_buffer[bytes_read+LeftOverData_level];
				memset(GuardPtr, 0, MAD_BUFFER_GUARD);
			}

			if (bytes_read > 0){
				mp3_src_pos+=bytes_read;
				mad_stream_buffer(&Mp3Stream, mp3_src_buffer, bytes_read+LeftOverData_level);
				Mp3Stream.error=MAD_ERROR_NONE;
			}
			else{
				goto end;	//EOF.
			}

			LeftOverData_level=0;
		}

		do{
			if((ret=mad_frame_decode(&Mp3Frame, &Mp3Stream))==0){
				mad_synth_frame(&Mp3Synth, &Mp3Frame);

				NumMP3SamplesConverted=Mp3Output(&Mp3Synth.pcm, output, samples-TotalConvertedSamples);
				output+=NumMP3SamplesConverted;
				TotalConvertedSamples+=NumMP3SamplesConverted;
			}
			else{
				if(MAD_RECOVERABLE(Mp3Stream.error)){
					if(Mp3Stream.error!=MAD_ERROR_LOSTSYNC && Mp3Stream.this_frame!=GuardPtr){
						lprintf("decoding error 0x%04x (%s) at byte offset %u\n", Mp3Stream.error, mad_stream_errorstr(&Mp3Stream), Mp3Stream.this_frame - Mp3Stream.buffer);
					}
				}else{
					if(Mp3Stream.error==MAD_ERROR_BUFLEN) break;
					else{
						lprintf("(Unrecoverable) decoding error 0x%04x (%s) at byte offset %u\n", Mp3Stream.error, mad_stream_errorstr(&Mp3Stream), Mp3Stream.this_frame - Mp3Stream.buffer);
						goto end;
					}
				}

				Mp3Stream.error=MAD_ERROR_NONE;
			}
		}while((TotalConvertedSamples<samples) && (samples-TotalConvertedSamples>=SAMPLES_PER_FRAME*2));
	}
end:
	return TotalConvertedSamples;
}

static void FillOutputBuffer(void)
{
	OutputBufferLevel=ConvertMP3(mp3_mix_buffer, OUT_BUFFER_SIZE);
	buffer_rd_ptr=0;
}

// might be called before initialization
int mp3_get_bitrate(FILE *f, int size)
{
	int ret, retval = -1, bytes_read, i;
	struct mad_stream TempMp3Stream;
	struct mad_header TempMp3Header;

	// filenames are stored instead handles in PSP, due to stupid max open file limit
	char *fname = (char *)f;

	if (mp3_handle != NULL) fclose(mp3_handle);
	if((mp3_handle = fopen(fname, "rb"))==NULL){
		lprintf("fopen(%s) failed\n", fname);
		goto end;
	}

	mad_stream_init(&TempMp3Stream);
	mad_header_init(&TempMp3Header);

	for(i=0; i<32;){
		if(TempMp3Stream.buffer==NULL || TempMp3Stream.error==MAD_ERROR_BUFLEN){
			if(TempMp3Stream.next_frame!=NULL){	//When there's a partial frame at the end of the buffer.
				LeftOverData_level=TempMp3Stream.bufend-TempMp3Stream.next_frame;
				memmove(mp3_src_buffer, TempMp3Stream.next_frame, LeftOverData_level);
			}

			if((bytes_read = fread(mp3_src_buffer+LeftOverData_level, 1, IN_BUFFER_SIZE-LeftOverData_level, mp3_handle))!=IN_BUFFER_SIZE-LeftOverData_level){
				//EOF reached.
				GuardPtr=&mp3_src_buffer[bytes_read+LeftOverData_level];
				memset(GuardPtr, 0, MAD_BUFFER_GUARD);
			}

			if (bytes_read > 0){
				mad_stream_buffer(&TempMp3Stream, mp3_src_buffer, bytes_read+LeftOverData_level);
				TempMp3Stream.error=MAD_ERROR_NONE;
			}
			else{
				goto end;	//EOF.
			}

			LeftOverData_level=0;
		}

		while(i<32){
			i++;

			if((ret=mad_header_decode(&TempMp3Header, &TempMp3Stream))==0){
				if (TempMp3Header.samplerate == 44100){	// only 44kHz supported..
					retval = TempMp3Header.bitrate/1000;
				}
				else{
					lprintf("mp3_get_bitrate: unsupported samplerate (%s): %u\n", fname, TempMp3Header.samplerate);
				}

				goto end;
			}
			else{
				if(MAD_RECOVERABLE(TempMp3Stream.error)){
					if(Mp3Stream.error!=MAD_ERROR_LOSTSYNC && Mp3Stream.this_frame!=GuardPtr){
						lprintf("mp3_get_bitrate: decoding error 0x%04x (%s) at byte offset %u\n", TempMp3Stream.error, mad_stream_errorstr(&TempMp3Stream), TempMp3Stream.this_frame - TempMp3Stream.buffer);
					}

					//Let it continue.
				}else{
					if(TempMp3Stream.error==MAD_ERROR_BUFLEN) break;	//Break out of inner loop.
					else{
						lprintf("mp3_get_bitrate: (Unrecoverable) decoding error 0x%04x (%s) at byte offset %u\n", TempMp3Stream.error, mad_stream_errorstr(&TempMp3Stream), TempMp3Stream.this_frame - TempMp3Stream.buffer);
						goto end;
					}
				}

				TempMp3Stream.error=MAD_ERROR_NONE;
			}
		}
	}

	if(i>=32){
		lprintf("mp3_get_bitrate: Can't locate valid MP3 frame.\n");
	}

end:
	mad_header_finish(&TempMp3Header);
	mad_stream_finish(&TempMp3Stream);

	if (mp3_handle != NULL) fclose(mp3_handle);
	mp3_handle = NULL;
	mp3_fname = NULL;
	if (retval < 0) mp3_last_error = -1; // remember we had a problem..
	return retval;
}

void mp3_start_play(FILE *f, int pos)
{
	char *fname = (char *)f;

	if (!initialized) return;

	lprintf("mp3_start_play(%s) @ %i\n", fname, pos);

	if (mp3_fname != fname || mp3_handle == NULL)
	{
		if (mp3_handle !=NULL) fclose(mp3_handle);
		if((mp3_handle = fopen(fname, "rb"))==NULL){
			lprintf("fopen(%s) failed\n", fname);
			return;
		}
		fseek(mp3_handle, 0, SEEK_END);
		mp3_src_size = ftell(mp3_handle);
		mp3_fname = fname;
	}

	// seek..
	mp3_src_pos = (int) (((float)pos / 1023.0f) * (float)mp3_src_size);
	fseek(mp3_handle, mp3_src_pos, SEEK_SET);
	lprintf("seek %i: %i/%i\n", pos, mp3_src_pos, mp3_src_size);

	buffer_rd_ptr=0;
	OutputBufferLevel=0;
	LeftOverData_level=0;
	Mp3Stream.buffer=NULL;
	Mp3Stream.this_frame=Mp3Stream.next_frame=NULL;
	Mp3Stream.error=MAD_ERROR_NONE;
	GuardPtr=&mp3_src_buffer[IN_BUFFER_SIZE-MAD_BUFFER_GUARD];

	/* send a request to start decoding a block of samples */
	FillOutputBuffer();
}

static void mix_audio(int *output, short int *input, unsigned int samples){
	int shr = 0;
	void (*mix_samples)(int *dest_buf, short *mp3_buf, int count) = mix_16h_to_32;
	if (PsndRate == 22050) { mix_samples = mix_16h_to_32_s1; shr = 1; }
	else if (PsndRate == 11025) { mix_samples = mix_16h_to_32_s2; shr = 2; }

	mix_samples(output, input, samples<<1);
}

void mp3_update(int *buffer, int length, int stereo)	// length = number of samples per channel.
{
	int length_mp3, NumSamplesCopied, NumSamplesToCopy, NumMp3SamplesCopied;

	// playback was started, track not ended
	if (mp3_handle == NULL || (mp3_src_pos >= mp3_src_size && OutputBufferLevel==0)) return;

	length*=2;	//It has to be fixed to stereo.
	length_mp3 = length;
	if (PsndRate == 22050) length_mp3 <<= 1;	// mp3s are locked to 44100Hz stereo
	else if (PsndRate == 11025) length_mp3 <<= 2;	// so make length 44100ish

	/* mix mp3 data, only stereo */
	for (NumSamplesCopied=0,NumMp3SamplesCopied=0; NumSamplesCopied<length;)
	{
		if(mp3_src_pos >= mp3_src_size && OutputBufferLevel==0) break;	//Don't wait for nothing! If the end of the track has been reached, don't wait for the decoding thread to decode samples because there's nothing left to decode!

		if(OutputBufferLevel>0){
			//Copy out whatever that's currently available.
			NumSamplesToCopy=OutputBufferLevel>(length_mp3-NumMp3SamplesCopied)?(length_mp3-NumMp3SamplesCopied):OutputBufferLevel;
			mix_audio(&buffer[NumSamplesCopied], &mp3_mix_buffer[buffer_rd_ptr], NumSamplesToCopy/2);

			NumMp3SamplesCopied+=NumSamplesToCopy;
			buffer_rd_ptr+=NumSamplesToCopy;
			OutputBufferLevel-=NumSamplesToCopy;

			if (PsndRate == 22050) NumSamplesCopied+=(NumSamplesToCopy/2);
			else if (PsndRate == 11025) NumSamplesCopied+=(NumSamplesToCopy/4);
			else NumSamplesCopied+=NumSamplesToCopy;
		}
		else{
			FillOutputBuffer();
		}
	}
}

int mp3_get_offset(void) // 0-1023
{
	unsigned int offs1024 = 0;
	int cdda_on;

	cdda_on = (PicoAHW & PAHW_MCD) && (PicoOpt&0x800) && !(Pico_mcd->s68k_regs[0x36] & 1) &&
			(Pico_mcd->scd.Status_CDC & 1) && mp3_handle != NULL;

	if (cdda_on) {
		offs1024  = mp3_src_pos << 7;
		offs1024 /= mp3_src_size >> 3;
	}
	lprintf("offs1024=%u (%i/%i)\n", offs1024, mp3_src_pos, mp3_src_size);

	return offs1024;
}
