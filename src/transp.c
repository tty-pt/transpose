#include <ctype.h>
#include <err.h>
#include <locale.h>
#include <qdb.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

struct space_queue {
	unsigned len;
	unsigned start;
	TAILQ_ENTRY(space_queue) entries;
};

TAILQ_HEAD(queue_head, space_queue) queue;

enum flags {
	TF_HIDE_CHORDS = 1,
	TF_HIDE_LYRICS = 2,
	TF_HTML = 4,
	TF_BEMOL = 8, // bemol instead of sustain
	TF_REMOVE_COMMENTS = 16,
	TF_BREAK_SLASH = 32,
	TF_PRINT_SHIFT = 64, // print different shifts
};

int flags = 0;
int skip_empty = 0, not_special = 0;
DB_TXN *txnid;
unsigned key = -1;

static char *chromatic_en[] = {
	"C\0",
	"C#\0Db",
	"D\0",
	"D#\0Eb",
	"E\0",
	"F\0",
	"F#\0Gb",
	"G\0",
	"G#\0Ab",
	"A\0",
	"A#\0Bb",
	"B\0",
	NULL,
}, *chromatic_latin[] = {
	"Do\0",
	"Do#\0Reb",
	"Re\0",
	"Re#\0Mib",
	"Mi\0",
	"Fa\0",
	"Fa#\0Solb",
	"Sol\0",
	"Sol#\0Lab",
	"La\0",
	"La#\0Sib",
	"Si\0",
	NULL,
}, *special[] = {
	"|",
	":",
	"-",
	NULL,
};

static char **i18n_chord_table = chromatic_en;
int chord_db = -1, special_db = -1;

static inline char *
chord_str(size_t chord) {
	register char *str = i18n_chord_table[chord];
	if ((flags & TF_BEMOL) && strchr(str, '#'))
		str += strlen(str) + 1;
	return str;
}

wchar_t outbuf[BUFSIZ];

static inline int oprintf(wchar_t *so, wchar_t *fmt, ...) {
	int ret;
	va_list args;
	va_start(args, fmt);
	ret = vswprintf(so, sizeof(outbuf) - (so - outbuf), fmt, args);
	va_end(args);
	return ret;
}

__attribute__((noreturn))
void shift_print(void) {
	unsigned i;

	for (i = 0; i < 12; i++) {
		char *name = i18n_chord_table[i];
		long t = (long) i - key;
		if (t < 0)
			t += 12;
		printf("%s %ld\n", name, t);
	}

	exit(EXIT_SUCCESS);	
}

static inline void
proc_line(char *line, size_t linelen, int t)
{
	static wchar_t wline[BUFSIZ], *ws;
	char buf[8], *s = line;
	int not_bolded = 1, is_special = 0;
	unsigned j = 0;

	line[linelen - 1] = '\0';
	if (skip_empty && !strcmp(line, "")) {
		skip_empty = 0;
		return;
	}

	int si = 0, sim = 0;
	wchar_t outbuf[BUFSIZ], *o = outbuf;

	if (flags & TF_HTML) {
		si += oprintf(outbuf + si, L"<div>");
		if (isdigit(*line)) {
			char *dot = strchr(s, '.');
			if (!dot)
				goto end;

			size_t len = dot + 1 - line;
			sim += len;
			si += oprintf(outbuf + si, L"<b>%.*s</b>", len, line);
			s = line + len;
		}
	}

	o = outbuf + si;
	int no_space = 1, has_chords = 0;
	for (; *s;) {
		if (*s == ' ' || *s == '/') {
			char what = *s;
			if (flags & TF_HIDE_LYRICS) {
				if (no_space && has_chords) {
					*o++ = what;
					no_space = 0;
					continue;
				}
			} else if (!(flags & TF_HIDE_CHORDS))
				*o++ = what;
			s++;
			j++;
			continue;
		}

		if (*s == '%') {
			if (flags & TF_REMOVE_COMMENTS)
				skip_empty = 1;
			else if (not_bolded && (flags & TF_HTML))
				o += oprintf(o, L"<b class='comment'>%s</b>", s);
			else
				o += oprintf(o, L"%s", s);
			s += strlen(s);
			continue;
		}

		no_space = 1;

		int notflat = s[1] != '#' && s[1] != 'b';
		register char *eoc, *space_after, *slash_after;

		eoc = s + (!notflat ? 2 : 1);
		space_after = strchr(eoc, ' ');
		slash_after = strchr(eoc, '/');

		const unsigned *chord_r;
		unsigned chord = -1;

		memset(buf, 0, sizeof(buf));
		strncpy(buf, s, 1);

		chord_r = qmap_get(special_db, buf);
		if ((is_special = !!chord_r)) {
			strncpy(buf, s, space_after ? space_after - s : strlen(s));
			not_special = 0;
			chord = -1;
		} else {
			switch (*eoc) {
				case ' ':
				case '\n':
				case '\0':
				case '/':
				case 'm': break;
				default:
					  if (isdigit(*eoc) || !strncmp(eoc, "sus", 3)
							  || !strncmp(eoc, "add", 3)
							  || !strncmp(eoc, "maj", 3)
							  || !strncmp(eoc, "dim", 3))
						  break;
					  goto no_chord;
			}
		}

		if (slash_after && (!space_after || space_after > slash_after))
			space_after = slash_after;

		if (!is_special) {
			strncpy(buf, s, eoc - s);
			chord_r = qmap_get(chord_db, buf);
			if (!chord_r)
				goto no_chord;
			chord = *chord_r;
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
			if (key == (unsigned) -1) {
				key = chord;
				if (flags & TF_PRINT_SHIFT)
					shift_print();
			}
			new_cstr = chord_str(chord);
			len = strlen(buf);
			diff = strlen(new_cstr) - len;
			modlen = space_after ? space_after - eoc : strlen(eoc);
		}

		if (flags & TF_HIDE_CHORDS) {
			s = eoc + modlen;
			continue;
		}

		if (not_bolded && (flags & TF_HTML)) {
			o += oprintf(o, L"<b>", s);
			not_bolded = 0;
		}

		if (is_special) {
			s = eoc + modlen;
			o += oprintf(o, L"%s ", buf);
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

		o += oprintf(o, L"%s%s", new_cstr, buf);

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
	swprintf(wline, sizeof(wline), L"%s", line + sim);
	j = 0;
	o = outbuf + si;
	if (flags & TF_HIDE_LYRICS)
		return;
	ws = wline;
	if ((flags & TF_HTML) && !not_bolded) {
		o += oprintf(o, L"</b>");
		not_bolded = 1;
	}
	for (; *ws;) {
		// take differences in chord sizes into consideration.
		// So that they remain on top of the correct parts of the lyrics.
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
		if (*ws == L'<') {
			o += swprintf(o, sizeof(outbuf) - (o - outbuf), L"%ls", ws);
			j = 0;
			goto end;
		}
		if (flags & TF_BREAK_SLASH) {
			if (*ws == L'/') {
				ws += 2;
				*o++ = L'\n';
				j = 0;
				continue;
			} else
				*o++ = *ws;
		} else
			*o++ = *ws;
		ws++;
		j++;
	}

end:
	if (flags & TF_HTML) {
		if (!not_bolded) {
			o += oprintf(o, L"</b>");
			not_bolded = 1;
		}
		if (o - outbuf < 6 && !has_chords)
			*o++ = L' ';
		o += oprintf(o, L"</div>");
	} else
		*o++ = L'\n';
	*o = '\0';
	wprintf(L"%ls", outbuf);
}

static inline void tbl_init(unsigned hd, char **table) {
	for (unsigned u = 0; ; u++) {
		char *key = table[u];
		if (!key)
			break;
		qmap_put(hd, key, &u);
		key += strlen(key) + 1;
		if (*key)
			qmap_put(hd, key, &u);
	}
}

int main(int argc, char *argv[]) {
	char *line = NULL;
	char c;
	ssize_t linelen;
	size_t linesize;
	int t = 0;

	while ((c = getopt(argc, argv, "t:hBblCLcs")) != -1) switch (c) {
		case 'h':
			  flags |= TF_HTML;
			  break;
		case 'b':
			  flags |= TF_BEMOL;
			  break;
		case 'B':
			  flags |= TF_BREAK_SLASH;
			  break;
		case 'c':
			  flags |= TF_REMOVE_COMMENTS;
			  break;
		case 'C':
			  flags |= TF_HIDE_CHORDS;
			  break;
		case 'l':
			  i18n_chord_table = chromatic_latin;
			  break;
		case 'L':
			  flags |= TF_HIDE_LYRICS;
			  break;
		case 's':
			  flags |= TF_PRINT_SHIFT;
			  break;
		case 't':
			  t = atoi(optarg);
	}

	if (t < 0)
		t += (1 + (t / 12)) * 12;

	unsigned unsigned_type = qmap_reg(sizeof(unsigned));
	chord_db = qmap_open(QM_STR, unsigned_type, 0x1F, 0);
	special_db = qmap_open(QM_STR, unsigned_type, 0xF, 0);

	setlocale(LC_ALL, "en_US.UTF-8");
	tbl_init(chord_db, chromatic_en);
	/* tbl_init(chord_db, chromatic_latin); */
	tbl_init(special_db, special);
	TAILQ_INIT(&queue);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
