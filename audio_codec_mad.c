#include <mad.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "audio_shared.h"
#include "audio_codec_mad.h"

// TODO: remove logger from codecs
#include "logger.h"

/*
 * libmad API: http://www.underbit.com/products/mad/
 */

static void *fdm;
static struct stat file_stat;

struct buffer {
	unsigned char const *start;
	unsigned long length;
};

char *buf;

static int set_audio_format_mad();

static enum mad_flow in_func(void *data, struct mad_stream *stream);
static enum mad_flow hdr_func(void *data, struct mad_header const *header);

static enum mad_flow
out_func(void *data, struct mad_header const *header, struct mad_pcm *pcm);

static enum mad_flow
err_func(void *data, struct mad_stream *stream, struct mad_frame *frame);

int
prepare_mad_codec()
{
	int fd;

	fd = open(current_filename, O_RDONLY);
	if (fd == -1) {
		logger("ERROR: can't open audio file\n");
		logger("%s\n", strerror(errno));
		return (-1);
	}

	if (fstat(fd, &file_stat) == -1) {
		logger("ERROR: fstat\n");
		logger("%s\n", strerror(errno));
		close(fd);
		return (-1);
	}

	if (file_stat.st_size == 0) {
		logger("ERROR - empty file");
		close(fd);
		return (-1);
	}

	fdm = mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (fdm == MAP_FAILED) {
		logger("ERROR: mmap\n");
		logger("%s\n", strerror(errno));
		close(fd);
		return (-1);
	}
	close(fd);

	set_audio_format_mad();
	if (open_audio_device() == -1)
		return (-1);
	return (0);
}

int
cleanup_mad_codec()
{
	int err;

	err = munmap(fdm, file_stat.st_size);
	if (err == -1) {
		return (-1);
	}

	ao_close(device);
	return (0);
}

exit_reason_t
play_file_using_mad_codec()
{
	int err;
	unsigned int buf_len;
	struct buffer mad_buffer;
	struct mad_decoder decoder;
	info_t status;

	buf_len = format.bits/8 * format.channels * format.rate * 2;
	buf = malloc(buf_len * sizeof (int));
	if (buf == NULL) {
		return (EXIT_REASON_ERROR);
	}

	mad_buffer.start  = fdm;
	mad_buffer.length = file_stat.st_size;
	mad_decoder_init(&decoder, &mad_buffer,
		in_func, 0, 0, out_func, err_func, 0);

	err = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
	logger("play_file: mad_decoder_run() returned %d\n", err);

	// TODO: check status
	pthread_mutex_lock(&audio_cmd_mutex);
	status = audio_cmd;
	pthread_mutex_unlock(&audio_cmd_mutex);
	switch (status) {
	case EXIT_REASON_STOP:
		logger("status: EXIT_REASON_STOP\n");
		break;
	default:
		logger("status: %d\n", status);
	}

	mad_decoder_finish(&decoder);
	free(buf);
	return (EXIT_REASON_EOF);
}

static enum mad_flow
in_func(void *data, struct mad_stream *stream)
{
	struct buffer *mad_buffer = data;

	if (!mad_buffer->length)
		return MAD_FLOW_STOP;

	mad_stream_buffer(stream, mad_buffer->start, mad_buffer->length);

	mad_buffer->length = 0;

	return MAD_FLOW_CONTINUE;
}

static int
set_audio_format_mad()
{
	struct buffer mad_buffer;
	struct mad_decoder decoder;
	int err;

	mad_buffer.start  = fdm;
	mad_buffer.length = file_stat.st_size;

	logger("set_audio_format_mad()\n");
	/* configure to get header information */
	mad_decoder_init(&decoder, &mad_buffer,
		in_func, hdr_func, 0, out_func, err_func, 0);

	err = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
	logger("header: mad_decoder_run() returned %d\n", err);

	mad_decoder_finish(&decoder);
	return (0);
}

static enum mad_flow
hdr_func(void *data, struct mad_header const *header)
{
	logger("MAD: hdr_func() layer %d, mode %d, bitrate %ld, samplerate %d\n",
		header->layer,
		header->mode,
		header->bitrate,
		header->samplerate
		);

	memset(&format, 0, sizeof (format));

	switch (header->mode) {
	case(MAD_MODE_SINGLE_CHANNEL):
		format.channels = 1;
		break;
	case(MAD_MODE_DUAL_CHANNEL):
		format.channels = 2;
		break;
	case(MAD_MODE_JOINT_STEREO):
		format.channels = 2;
		break;
	case(MAD_MODE_STEREO):
		format.channels = 2;
		break;
	default: // TODO
		format.channels = 2;
		;;
	}
	format.rate = header->samplerate;
	format.byte_format = AO_FMT_NATIVE;
	format.bits = 16;

	return MAD_FLOW_STOP;
}

static inline signed int
downsample(mad_fixed_t sample)
{
	return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static enum mad_flow
out_func(void *data, struct mad_header const *header, struct mad_pcm *pcm)
{
	unsigned int i;
	mad_fixed_t const *left, *right;
	signed int sample;
	char *ptr;
	info_t command;
	bool paused;

	i = pcm->length;
	left = pcm->samples[0];
	right = pcm->samples[1];

	ptr = buf;
	while (i--) {
		sample = downsample(*left++);
		*ptr++ = sample & 0xff;
		*ptr++ = (sample >> 8) & 0xff;

		if (pcm->channels == 2) {
			sample = downsample(*right++);
			*ptr++ = sample & 0xff;
			*ptr++ = (sample >> 8) & 0xff;
		}
	}

	// checking for new event
	pthread_mutex_lock(&audio_cmd_mutex);
	command = audio_cmd;
	pthread_mutex_unlock(&audio_cmd_mutex);

	switch (command) {
	case CMD_PLAY:
		logger("engine_ao - CMD_PLAY\n");
		pthread_mutex_lock(&audio_cmd_mutex);
		audio_cmd = STATUS_STOP;
		pthread_mutex_unlock(&audio_cmd_mutex);
		// TODO: use codec_status only
		pthread_mutex_lock(&codec_status_mutex);
		codec_status = CODEC_STATUS_EXIT_PLAY_OTHER;
		pthread_mutex_unlock(&codec_status_mutex);
		return (MAD_FLOW_STOP);
		break;
	case CMD_STOP:
		logger("engine_ao - CMD_STOP\n");
		pthread_mutex_lock(&audio_cmd_mutex);
		audio_cmd = STATUS_STOP;
		pthread_mutex_unlock(&audio_cmd_mutex);
		return (MAD_FLOW_STOP);
	case CMD_QUIT:
		logger("engine_ao - CMD_QUIT\n");
		pthread_mutex_lock(&audio_cmd_mutex);
		audio_cmd = STATUS_EXIT;
		pthread_mutex_unlock(&audio_cmd_mutex);
		return (MAD_FLOW_STOP);
	case CMD_PAUSE: // TODO
		//paused = true;
		break;
	case CMD_FF: // TODO
		break;
	case CMD_REV: // TODO
		break;
	case STATUS_ACK:
		break;
	default:
		logger("engine_ao - TODO: %d\n", command);
		;;
	}
	ao_play(device, buf, pcm->length * pcm->channels * 2);

	return (MAD_FLOW_CONTINUE);
}

static enum mad_flow
err_func(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
	return MAD_FLOW_CONTINUE;
}
