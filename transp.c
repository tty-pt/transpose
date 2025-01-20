// TODO use qhash
#include <ctype.h>
#include <err.h>
#include <locale.h>
#include <qhash.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define SHASH_INIT			hash_init
#define SHASH_GET(hd, key)		hash_get(hd, key, strlen(key))
#define SHASH_DEL(hd, key)		hash_del(hd, key, strlen(key))

#define HASH_DBS_MAX 256
#define HASH_NOT_FOUND ((size_t) -1)

struct space_queue {
	unsigned len;
	unsigned start;
	TAILQ_ENTRY(space_queue) entries;
};

TAILQ_HEAD(queue_head, space_queue) queue;

size_t hash_n = 0;
wchar_t chorus[BUFSIZ], *chorus_p = chorus;
int reading_chorus = 0, skip_empty = 0, not_special = 0;
DB_TXN *txnid;

static char *chromatic_en[] = {
	"C",
	"C#\0Db",
	"D",
	"D#\0Eb",
	"E",
	"F",
	"F#\0Gb",
	"G",
	"G#\0Ab",
	"A",
	"A#\0Bb",
	"B",
	NULL,
}, *chromatic_latin[] = {
	"Do",
	"Do#\0Reb",
	"Re",
	"Re#\0Mib",
	"Mi",
	"Fa",
	"Fa#\0Solb",
	"Sol",
	"Sol#\0Lab",
	"La",
	"La#\0Sib",
	"Si",
	NULL,
}, *special[] = {
	"|",
	":",
	"-",
	NULL,
};

static char **i18n_chord_table = chromatic_en;
int chord_db = -1, special_db = -1;
int html = 0, bemol = 0, hide_chords = 0, hide_lyrics = 0;

static inline char *
chord_str(size_t chord) {
	register char *str = i18n_chord_table[chord];
	if (bemol && strchr(str, '#'))
		str += strlen(str) + 1;
	return str;
}

static inline void
proc_line(char *line, size_t linelen, int t)
{
	static wchar_t wline[BUFSIZ], *ws;
	char buf[8], c, *s = line;
	int not_bolded = 1, is_special = 0;
	unsigned j = 0;

	line[linelen - 1] = '\0';
	if (skip_empty && !strcmp(line, "")) {
		skip_empty = 0;
		return;
	} else if (!strcmp(line, "-- Chorus")) {
		wprintf(L"%ls", chorus);
		skip_empty = 1;
		return;
	} else if (!strcmp(line, "-- Chorus start")) {
		skip_empty = 1;
		reading_chorus = 1;
		return;
	} else if (!strcmp(line, "-- Chorus end")) {
		skip_empty = 1;
		reading_chorus = 0;
		return;
	}

	wchar_t outbuf[BUFSIZ], *so = outbuf, *o = outbuf;

	if (html) {
		so += swprintf(so, sizeof(outbuf) - (so - outbuf), L"<div>");
		if (isdigit(*line)) {
			char *dot = strchr(s, '.');
			if (!dot)
				goto end;

			size_t len = dot + 1 - line;

			so += swprintf(so, sizeof(outbuf) - (so - outbuf), L"<b>%.*s</b>", len, line);
			s = line + len;
		}
	}

	o = so;
	int no_space = 1, has_chords = 0;
	for (; *s;) {
		if (*s == ' ' || *s == '/') {
			char what = *s;
			if (hide_lyrics) {
				if (no_space && has_chords) {
					*o++ = what;
					no_space = 0;
					continue;
				}
			} else if (!hide_chords)
				*o++ = what;
			s++;
			j++;
			continue;
		}

		no_space = 1;

		int notflat = s[1] != '#' && s[1] != 'b';
		register char *eoc, *space_after, *slash_after;

		eoc = s + (!notflat ? 2 : 1);
		space_after = strchr(eoc, ' ');
		slash_after = strchr(eoc, '/');

		unsigned chord = -1;

		memset(buf, 0, sizeof(buf));
		strncpy(buf, s, 1);

		if ((is_special = !shash_get(special_db, &chord, buf))) {
			strncpy(buf, s, space_after ? space_after - s : strlen(s));
			not_special = 0;
			chord = -1;
		} else switch (*eoc) {
			case ' ':
			case '\n':
			case '\0':
			case '/':
			case 'm': break;
			default:
				  if (isdigit(*eoc) || !strncmp(eoc, "sus", 3) || !strncmp(eoc, "add", 3) || !strncmp(eoc, "maj", 3))
						  break;
				  goto no_chord;
		}

		if (slash_after && (!space_after || space_after > slash_after))
			space_after = slash_after;

		if (!is_special) {
			/* memset(buf, 0, sizeof(buf)); */
			strncpy(buf, s, eoc - s);
			if (shash_get(chord_db, &chord, buf)) {
				o = outbuf;
				goto no_chord;
				/* o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"%s", buf); */
			}
		}

		char *new_cstr;
		int len, diff, modlen, i;

		has_chords = 1;

		if (is_special) {
			new_cstr = s;
			diff = 0;
			modlen = strlen(buf);
		} else {
			chord = (chord + t) % 12;
			new_cstr = chord_str(chord);
			len = strlen(buf);
			diff = strlen(new_cstr) - len;
			modlen = space_after ? space_after - eoc : strlen(eoc);
		}

		if (hide_chords) {
			s = eoc + modlen;
			continue;
		}

		if (not_bolded && html) {
			o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"<b>");
			not_bolded = 0;
		}

		if (is_special) {
			s = eoc + modlen;
			o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"%s ", buf);
			continue;
		}

		memset(buf, 0, sizeof(buf));
		strncpy(buf, eoc, modlen);
		j += eoc - s + modlen;
		s = eoc + modlen;

		for (i = 0; i < diff && *s == ' '; i++, s++, j++) ;

		if (*s == '\0')
			for (i = 0; i < diff; i++, j++) ;

		if (buf[0] == 'm' && i18n_chord_table == chromatic_latin)
			buf[0] = '-';

		o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"%s%s", new_cstr, buf);

		if (*s != ' ' && *s != '/' && s + 1 < line + linelen) {
			*o++ = L' ';
			diff++;
		}

		if (i < diff) {
			struct space_queue *new_element = malloc(sizeof(struct space_queue));
			new_element->len = diff - i;
			new_element->start = j;
			TAILQ_INSERT_TAIL(&queue, new_element, entries);
			j += diff - i;
		}
	}

	goto end;

no_chord:
	mbstowcs(wline, s, sizeof(wline));
	j = 0;
	o = so;
	if (hide_lyrics)
		return;
	ws = wline;
	if (html) {
		if (!not_bolded) {
			o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"</b>");
			not_bolded = 1;
		}
	}
	for (; *ws;) {
		if (!TAILQ_EMPTY(&queue)) {
			struct space_queue *first = TAILQ_FIRST(&queue);
			if (j >= first->start) {
				if (not_special) while (j < first->start + first->len) {
					wchar_t c = *(ws - 1) == ' ' ? ' ' : '-';
					*o++ = c;
					j++;
				}
				struct space_queue *first = TAILQ_FIRST(&queue);
				TAILQ_REMOVE(&queue, first, entries);
				free(first);
			}
		}
		*o++ = *ws;
		ws++;
		j++;
	}

end:
	if (html) {
		if (!not_bolded) {
			o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"</b>");
			not_bolded = 1;
		}
		if (o - outbuf < 6 && !has_chords)
			*o++ = L' ';
		o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"</div>");
	} else
		*o++ = L'\n';
	*o = '\0';
	wprintf(L"%ls", outbuf);
	if (reading_chorus) {
		memcpy(chorus_p, outbuf, (o + 1 - outbuf) * sizeof(wchar_t));
		chorus_p += o - outbuf;
	}
}

int main(int argc, char *argv[]) {
	char *line = NULL;
	char buf[8], c;
	ssize_t linelen;
	size_t linesize;
	int t = 0;

	chord_db = hash_init();
	special_db = hash_init();

	while ((c = getopt(argc, argv, "t:hblCL")) != -1) switch (c) {
		case 'h':
			  html = 1;
			  break;
		case 'b':
			  bemol = 1;
			  break;
		case 'C':
			  hide_chords = 1;
			  break;
		case 'l':
			  i18n_chord_table = chromatic_latin;
			  break;
		case 'L':
			  hide_lyrics = 1;
			  break;
		case 't':
			  t = atoi(optarg);
	}

	if (t < 0)
		t += (1 + (t / 12)) * 12;

	setlocale(LC_ALL, "en_US.UTF-8");
	suhash_table(chord_db, chromatic_en);
	suhash_table(special_db, special);
	TAILQ_INIT(&queue);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
