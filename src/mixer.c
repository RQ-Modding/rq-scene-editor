/* The sounds of Play from here: several at once on one SDL device, read with libsndfile from
 * memory and brought to 44100 Hz stereo. The music plays on two channels as the game's
 * SYS_PlayBGM does: a new track starts silent and rises over the fade's time while the old one
 * falls over the same time and stops; the same track again changes nothing; a fade type of 0 (the
 * game's default, 音楽設定(0, 0)) switches at once. A track loops as the game's .bgi says.
 *
 * Rance Browser's player (one sound at a time) stays the fields' Play button. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#include "system4.h"

#include "editor.h"

#define RATE 44100
#define VOICES 16
#define CHUNK 1024

struct vio {
	uint8_t *data;
	sf_count_t size, pos;
};

static sf_count_t vio_len(void *u) { return ((struct vio *)u)->size; }
static sf_count_t vio_tell(void *u) { return ((struct vio *)u)->pos; }

static sf_count_t vio_seek(sf_count_t off, int whence, void *u)
{
	struct vio *v = u;
	sf_count_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? v->pos + off : v->size + off;
	if (p < 0) p = 0;
	if (p > v->size) p = v->size;
	v->pos = p;
	return p;
}

static sf_count_t vio_read(void *ptr, sf_count_t n, void *u)
{
	struct vio *v = u;
	if (n > v->size - v->pos)
		n = v->size - v->pos;
	memcpy(ptr, v->data + v->pos, (size_t)n);
	v->pos += n;
	return n;
}

static sf_count_t vio_write(const void *ptr, sf_count_t n, void *u)
{
	(void)ptr; (void)n; (void)u;
	return 0;
}

static SF_VIRTUAL_IO vio_ops = { vio_len, vio_seek, vio_read, vio_write, vio_tell };

struct voice {
	bool on, music;
	int no;
	struct vio v;
	SNDFILE *f;
	SF_INFO info;
	bool loop;
	sf_count_t loop_start, loop_end, pos;   /* in the file's frames */
	float rb[CHUNK * 2];                    /* frames read, as stereo */
	int rb_n, rb_i;
	float cur[2], next[2];
	double frac, step;
	bool primed, done;
	float vol, vol_from, vol_to;            /* 0 to 1 */
	long fade_left, fade_total;             /* in output frames */
	bool stop_after;
};

static SDL_AudioDeviceID dev;
static struct voice voices[VOICES];
static int music_now;                       /* the track the game would say is playing */
static bool offline;                        /* --mixer-check: no device, the samples made by hand */

static void lock(void)
{
	if (dev)
		SDL_LockAudioDevice(dev);
}

static void unlock(void)
{
	if (dev)
		SDL_UnlockAudioDevice(dev);
}

static void voice_free(struct voice *v)
{
	if (v->f)
		sf_close(v->f);
	free(v->v.data);
	memset(v, 0, sizeof *v);
}

/* The next frame of the file, stereo; false at its end. */
static bool src_frame(struct voice *v, float out[2])
{
	if (v->rb_i >= v->rb_n) {
		static float tmp[CHUNK * 8];
		int ch = v->info.channels;
		sf_count_t want = CHUNK * 8 / ch;
		if (want > CHUNK)
			want = CHUNK;
		bool looping = v->loop && v->loop_end > v->loop_start;
		if (looping && v->pos + want > v->loop_end)
			want = v->loop_end - v->pos;
		sf_count_t got = want > 0 ? sf_readf_float(v->f, tmp, want) : 0;
		if (got > 0)
			v->pos += got;
		if (looping && (v->pos >= v->loop_end || got < want)) {
			sf_seek(v->f, v->loop_start, SEEK_SET);
			v->pos = v->loop_start;
		}
		if (got <= 0)
			return false;
		for (sf_count_t i = 0; i < got; i++) {
			v->rb[i * 2] = tmp[i * ch];
			v->rb[i * 2 + 1] = tmp[i * ch + (ch > 1 ? 1 : 0)];
		}
		v->rb_n = (int)got;
		v->rb_i = 0;
	}
	out[0] = v->rb[v->rb_i * 2];
	out[1] = v->rb[v->rb_i * 2 + 1];
	v->rb_i++;
	return true;
}

static void callback(void *u, Uint8 *stream, int len)
{
	(void)u;
	float *out = (float *)stream;
	int frames = len / (int)(sizeof(float) * 2);
	memset(out, 0, (size_t)len);
	for (int n = 0; n < VOICES; n++) {
		struct voice *v = &voices[n];
		if (!v->on || v->done)
			continue;
		if (!v->primed) {
			if (!src_frame(v, v->cur)) {
				v->done = true;
				continue;
			}
			if (!src_frame(v, v->next))
				memcpy(v->next, v->cur, sizeof v->cur);
			v->primed = true;
		}
		for (int i = 0; i < frames; i++) {
			if (v->fade_left > 0) {
				v->fade_left--;
				float t = 1.0f - (float)v->fade_left / (float)v->fade_total;
				v->vol = v->vol_from + (v->vol_to - v->vol_from) * t;
				if (v->fade_left == 0 && v->stop_after) {
					v->done = true;
					break;
				}
			}
			float a = (float)v->frac;
			out[i * 2] += (v->cur[0] + (v->next[0] - v->cur[0]) * a) * v->vol;
			out[i * 2 + 1] += (v->cur[1] + (v->next[1] - v->cur[1]) * a) * v->vol;
			v->frac += v->step;
			while (v->frac >= 1.0) {
				v->frac -= 1.0;
				memcpy(v->cur, v->next, sizeof v->cur);
				if (!src_frame(v, v->next)) {
					v->done = true;
					break;
				}
			}
			if (v->done)
				break;
		}
	}
	for (int i = 0; i < frames * 2; i++)
		out[i] = out[i] > 1 ? 1 : out[i] < -1 ? -1 : out[i];
}

static bool open_device(void)
{
	if (dev || offline)
		return true;
	SDL_AudioSpec want = { 0 }, have;
	want.freq = RATE;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = callback;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!dev)
		return false;
	SDL_PauseAudioDevice(dev, 0);
	return true;
}

static struct audio_entry *entry_of(bool music, int no)
{
	struct audio_entry *list = music ? G.music : G.sounds;
	int n = music ? G.nmusic : G.nsounds;
	for (int i = 0; i < n; i++)
		if (list[i].no == no)
			return &list[i];
	return NULL;
}

/* A voice for sound or track `no`, silent until it is let go (the device locked by the caller). */
static struct voice *start(bool music, int no, float vol)
{
	struct audio_entry *a = entry_of(music, no);
	if (!a || !open_device())
		return NULL;
	size_t size = 0;
	uint8_t *data = game_read_audio(music ? G.h.ba : G.h.wa, no, &size);
	if (!data)
		return NULL;
	struct voice *v = NULL;
	lock();
	for (int i = 0; i < VOICES && !v; i++)
		if (!voices[i].on || voices[i].done)
			v = &voices[i];
	if (v)
		voice_free(v);
	unlock();
	if (!v) {
		free(data);
		return NULL;
	}
	/* Filled in its place (libsndfile keeps the address of its reader); the callback passes over
	 * it until it is on. */
	v->v.data = data;
	v->v.size = (sf_count_t)size;
	v->f = sf_open_virtual(&vio_ops, SFM_READ, &v->info, &v->v);
	if (!v->f || v->info.channels <= 0 || v->info.samplerate <= 0) {
		voice_free(v);
		return NULL;
	}
	v->music = music;
	v->no = no;
	v->loop = music && a->has_loop;
	v->loop_start = a->loop_start;
	v->loop_end = a->loop_end;
	v->step = (double)v->info.samplerate / RATE;
	v->vol = vol;
	lock();
	v->on = true;
	unlock();
	return v;
}

static void fade(struct voice *v, float to, int ms, bool stop)
{
	long n = (long)ms * RATE / 1000;
	if (n <= 0) {
		v->vol = to;
		v->fade_left = 0;
		if (stop)
			v->done = true;
		return;
	}
	v->vol_from = v->vol;
	v->vol_to = to;
	v->fade_total = v->fade_left = n;
	v->stop_after = stop;
}

void mixer_sound(int no)
{
	if (no > 0)
		start(false, no, 1.0f);
}

void mixer_music(int no, int effect, int ms)
{
	/* SYS_PlayBGM: -1 the defaults (音楽設定(0, 0)), a type of 0 no fade. */
	if (effect < 0)
		effect = 0;
	if (ms < 0)
		ms = 0;
	if (effect == 0)
		ms = 0;
	if (no != 0 && !entry_of(true, no))
		no = 0;
	if (no == music_now)
		return;
	lock();
	for (int i = 0; i < VOICES; i++)
		if (voices[i].on && voices[i].music && !voices[i].done && !voices[i].stop_after)
			fade(&voices[i], 0, ms, true);
	unlock();
	music_now = no;
	if (no == 0)
		return;
	struct voice *v = start(true, no, ms > 0 ? 0.0f : 1.0f);
	if (v && ms > 0) {
		lock();
		fade(v, 1.0f, ms, false);
		unlock();
	}
}

int mixer_music_now(void)
{
	return music_now;
}

void mixer_stop(int ms)
{
	music_now = 0;
	lock();
	for (int i = 0; i < VOICES; i++)
		if (voices[i].on && !voices[i].done)
			fade(&voices[i], 0, ms, true);
	unlock();
}

double mixer_sound_ms(int no)
{
	struct audio_entry *a = entry_of(false, no);
	if (!a)
		return 0;
	size_t size = 0;
	uint8_t *data = game_read_audio(G.h.wa, no, &size);
	if (!data)
		return 0;
	struct vio v = { data, (sf_count_t)size, 0 };
	SF_INFO info = { 0 };
	SNDFILE *f = sf_open_virtual(&vio_ops, SFM_READ, &info, &v);
	double ms = f && info.samplerate > 0 ? (double)info.frames * 1000.0 / info.samplerate : 0;
	if (f)
		sf_close(f);
	free(data);
	return ms;
}

void mixer_shutdown(void)
{
	if (dev) {
		SDL_CloseAudioDevice(dev);
		dev = 0;
	}
	for (int i = 0; i < VOICES; i++)
		voice_free(&voices[i]);
	music_now = 0;
}

/* ---- --mixer-check: the mixer without a device, its samples counted ---- */

static int mc_fail, mc_count;

static void mc_expect(bool ok, const char *what)
{
	mc_count++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		mc_fail++;
}

/* ms of output; the loudest sample. */
static float render(int ms)
{
	static float buf[1024 * 2];
	float peak = 0;
	long left = (long)ms * RATE / 1000;
	while (left > 0) {
		int n = left > 1024 ? 1024 : (int)left;
		callback(NULL, (Uint8 *)buf, n * 2 * (int)sizeof(float));
		for (int i = 0; i < n * 2; i++)
			peak = fmaxf(peak, fabsf(buf[i]));
		left -= n;
	}
	return peak;
}

static struct voice *voice_of(bool music, int no)
{
	for (int i = 0; i < VOICES; i++)
		if (voices[i].on && !voices[i].done && voices[i].music == music && voices[i].no == no)
			return &voices[i];
	return NULL;
}

static int live(void)
{
	int n = 0;
	for (int i = 0; i < VOICES; i++)
		n += voices[i].on && !voices[i].done;
	return n;
}

int mixer_check(void)
{
	char what[200];
	offline = true;
	mc_fail = mc_count = 0;
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Sounds: 408 (set's jingle) and 406 (the background's) at once. */
	mixer_sound(408);
	mixer_sound(406);
	mc_expect(voice_of(false, 408) && voice_of(false, 406), "two sounds at once");
	struct voice *s = voice_of(false, 408);
	snprintf(what, sizeof what, "sound 408 read: %d Hz, %d channels", s ? s->info.samplerate : 0, s ? s->info.channels : 0);
	mc_expect(s && s->info.samplerate > 0, what);
	float peak = render(300);
	snprintf(what, sizeof what, "the sounds heard (peak %.3f)", peak);
	mc_expect(peak > 0.01f, what);
	double ms = mixer_sound_ms(408);
	snprintf(what, sizeof what, "sound 408's length: %.0f ms", ms);
	mc_expect(ms > 100, what);
	s = voice_of(false, 408);
	if (s) {
		sf_count_t before = s->pos;
		render(700);
		s = voice_of(false, 408);
		if (s) {
			double got = (double)(s->pos - before), want = 0.7 * s->info.samplerate;
			snprintf(what, sizeof what, "0.7 s of output reads 0.7 s of the file (%.0f of %.0f frames)", got, want);
			mc_expect(fabs(got - want) < CHUNK + 8, what);
		}
	}
	render((int)ms + 500);
	mc_expect(!voice_of(false, 408), "a sound ends by itself");

	/* Two tracks with a loop. */
	int a = 0, b = 0;
	for (int i = 0; i < G.nmusic && !b; i++) {
		if (!G.music[i].has_loop || G.music[i].loop_end <= G.music[i].loop_start)
			continue;
		if (!a)
			a = G.music[i].no;
		else
			b = G.music[i].no;
	}
	snprintf(what, sizeof what, "two looping tracks in the game: %d and %d", a, b);
	mc_expect(a && b, what);
	if (a && b) {
		mixer_music(a, -1, -1);
		struct voice *va = voice_of(true, a);
		mc_expect(va && va->vol == 1.0f && mixer_music_now() == a, "a track with the defaults: at full volume at once");
		peak = render(200);
		mc_expect(peak > 0.001f, "the track heard");
		mixer_music(a, 1, 1000);
		mc_expect(live() == 1, "the same track again: nothing changes");
		mixer_music(b, 1, 1000);
		render(500);
		va = voice_of(true, a);
		struct voice *vb = voice_of(true, b);
		snprintf(what, sizeof what, "halfway through a fade of a second: %.2f falling, %.2f rising", va ? va->vol : -1, vb ? vb->vol : -1);
		mc_expect(va && vb && fabsf(va->vol - 0.5f) < 0.05f && fabsf(vb->vol - 0.5f) < 0.05f, what);
		render(600);
		vb = voice_of(true, b);
		mc_expect(!voice_of(true, a) && vb && vb->vol == 1.0f, "after it: the old track stopped, the new one at full volume");
		if (vb) {
			sf_count_t start = vb->loop_start, end = vb->loop_end;
			lock();
			sf_seek(vb->f, end - 2000, SEEK_SET);
			vb->pos = end - 2000;
			vb->rb_n = vb->rb_i = 0;
			unlock();
			render(300);
			vb = voice_of(true, b);
			snprintf(what, sizeof what, "past the loop's end, back after its start (at %lld, the loop %lld to %lld)",
			         vb ? (long long)vb->pos : -1LL, (long long)start, (long long)end);
			mc_expect(vb && vb->pos > start && vb->pos < start + 20000, what);
		}
		mixer_music(0, 1, 200);
		render(300);
		mc_expect(!voice_of(true, b) && mixer_music_now() == 0, "music 0 fades the track out");
	}
	mixer_sound(408);
	mixer_stop(0);
	mc_expect(live() == 0, "stop: nothing left");
	mixer_shutdown();
	printf("%d/%d\n", mc_count - mc_fail, mc_count);
	fflush(stdout);
	return mc_fail ? 1 : 0;
}
