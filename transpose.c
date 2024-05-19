#include <stddef.h>
#include <err.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <wchar.h>
#include <locale.h>

#ifdef __OpenBSD__
#include <db4/db.h>
#else
#include <db.h>
#endif

#define SHASH_INIT			hash_init
#define SHASH_GET(hd, key)		hash_get(hd, key, sizeof(wchar_t) * wcslen(key))
#define SHASH_DEL(hd, key)		hash_del(hd, key, sizeof(wchar_t) * wcslen(key))

#define HASH_DBS_MAX 256
#define HASH_NOT_FOUND ((size_t) -1)

struct space_queue {
	unsigned len;
	unsigned start;
	TAILQ_ENTRY(space_queue) entries;
};

TAILQ_HEAD(queue_head, space_queue) queue;

DB *hash_dbs[HASH_DBS_MAX];

size_t hash_n = 0;
wchar_t chorus[BUFSIZ], *chorus_p = chorus;
int reading_chorus = 0, skip_empty = 0;

int
hash_init()
{
	DB **db = &hash_dbs[hash_n];
	if (db_create(db, NULL, 0) || (*db)->open(*db, NULL, NULL, NULL, DB_HASH, DB_CREATE, 0644))
		err(1, "hash_init");
	return hash_n++;
}

void
hash_put(int hd, void *key_r, size_t key_len, size_t id)
{
	DB *db = hash_dbs[hd];
	DBT key, data;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.data = (void *) key_r;
	key.size = key_len;
	data.data = &id;
	data.size = sizeof(size_t);

	if (db->put(db, NULL, &key, &data, 0))
		err(1, "hash_put");
}

size_t
hash_get(int hd, void *key_r, size_t key_len)
{
	DB *db = hash_dbs[hd];
	DBT key, data;
	int ret;

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.data = (void *) key_r;
	key.size = key_len;

	ret = db->get(db, NULL, &key, &data, 0);

	if (ret == DB_NOTFOUND)
		return -1;
	else if (ret)
		err(1, "hash_get");

	return * (size_t *) data.data;
}

void
hash_del(int hd, void *key_r, size_t len)
{
	DB *db = hash_dbs[hd];
	DBT key;

	memset(&key, 0, sizeof(key));
	key.data = key_r;
	key.size = len;

	if (db->del(db, NULL, &key, 0))
		err(1, "hash_del");
}

void
hash_table(int hd, wchar_t *table[]) {
	for (size_t i = 0; table[i]; i++) {
		wchar_t *str = table[i];
		size_t len = wcslen(str);
		hash_put(hd, str, len, i);
		str += len;
		if (*str)
			hash_put(hd, str, wcslen(str), i);
	}
}

static wchar_t *chromatic_en[] = {
	L"C",
	L"C#\0Db",
	L"D",
	L"D#\0Eb",
	L"E",
	L"F",
	L"F#\0Gb",
	L"G",
	L"G#\0Ab",
	L"A",
	L"A#\0Bb",
	L"B",
	NULL,
}, *chromatic_latin[] = {
	L"Do",
	L"Do#\0Reb",
	L"Re",
	L"Re#\0Mib",
	L"Mi",
	L"Fa",
	L"Fa#\0Solb",
	L"Sol",
	L"Sol#\0Lab",
	L"La",
	L"La#\0Sib",
	L"Si",
	NULL,
};

static wchar_t **i18n_chord_table = chromatic_en;
int chord_db = -1;
int html = 0, bemol = 0;
int prev_chord = 0;

static inline wchar_t *
chord_str(size_t chord) {
	register wchar_t *str = i18n_chord_table[chord];
	if (bemol && wcschr(str, L'#'))
		str += wcslen(str) + 1;
	return str;
}

static inline void
proc_line(wchar_t *wline, size_t linelen, int t)
{
	/* static wchar_t wline[BUFSIZ], *ws = wline; */
	wchar_t *ws = wline;
	wchar_t buf[8], c;
	int not_bolded = 1;
	unsigned j = 0;
	unsigned not_chords = 0;

	wline[linelen - 1] = '\0';
	/* mbstowcs(wline, line, sizeof(wline)); */
	if (prev_chord)
		goto no_chord;

	if (skip_empty && !wcscmp(wline, L"")) {
		skip_empty = 0;
		return;
	} else if (!wcscmp(wline, L"-- Chorus")) {
		wprintf(L"%ls", chorus);
		return;
	} else if (!wcscmp(wline, L"-- Chorus start")) {
		skip_empty = 1;
		reading_chorus = 1;
		return;
	} else if (!wcscmp(wline, L"-- Chorus end")) {
		skip_empty = 1;
		reading_chorus = 0;
		return;
	}

	for (ws = wline; *ws;) {
		if (*ws == L' ') {
			putwchar(L' ');
			if (reading_chorus)
				*chorus_p++ = L' ';
			ws++;
			j++;
			continue;
		}

		int notflat = ws[1] != L'#' && ws[1] != L'b';

		register wchar_t
			*eoc = ws + (!notflat ? 2 : 1),
			*space_after = wcschr(eoc, L' ');

		register size_t chord;

		memset(buf, 0, sizeof(buf));
		wcsncpy(buf, ws, eoc - ws);
		chord = SHASH_GET(chord_db, buf);

		if (chord == HASH_NOT_FOUND)
			goto no_chord;

		prev_chord = 1;

		if (not_bolded && html) {
			wprintf(L"<b>");
			if (reading_chorus)
				chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"<b>");
			not_bolded = 0;
		}

		chord = (chord + t) % 12;

		wchar_t *new_cstr = chord_str(chord);
		int len = wcslen(buf);
		int diff = wcslen(new_cstr) - len;
		int modlen, i;

		modlen = space_after ? space_after - eoc : wcslen(eoc);
		memset(buf, 0, sizeof(buf));
		wcsncpy(buf, eoc, modlen);
		j += eoc - ws + modlen;
		ws = eoc + modlen;
		wchar_t *is = ws;

		for (i = 0; i < diff && *ws == ' '; i++, ws++, j++) ;

		if (*ws == '\0')
			for (i = 0; i < diff; i++, j++) ;

		if (buf[0] == 'm' && i18n_chord_table == chromatic_latin)
			buf[0] = '-';

		wprintf(L"%s%ls", new_cstr, buf);
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"%s%ls", new_cstr, buf);

		if (*ws != L' ' && ws + 1 < wline + linelen) {
			putwchar(L' ');
			if (reading_chorus)
				*chorus_p++ = L' ';
			diff++;
		}

		if (i < diff) {
			struct space_queue *new_element = malloc(sizeof(struct space_queue));
			new_element->len = diff - i;
			new_element->start = j;
			j += diff - i;
			TAILQ_INSERT_TAIL(&queue, new_element, entries);
		}
	}

	if (html && ws == wline) {
		wprintf(L"<div> </div>");
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"<div> </div>");
	}

	if (html && !not_bolded) {
		wprintf(L"</b>");
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"</b>");
	}

	not_bolded = 1;
	if (!html) {
		putwchar('\n');
		if (reading_chorus)
			*chorus_p++ = L'\n';
	}
	return;

no_chord:
	prev_chord = 0;
	j = 0;
	if (html) {
		wprintf(L"<div>");
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"<div>");
	}
	for (ws = wline; *ws;) {
		if (!TAILQ_EMPTY(&queue)) {
			struct space_queue *first = TAILQ_FIRST(&queue);
			if (j >= first->start) {
				while (j < first->start + first->len) {
					wchar_t c = *(ws - 1) == L' ' ? L' ' : L'-';
					putwchar(c);
					if (reading_chorus)
						*chorus_p++ = c;
					j++;
				}
				struct space_queue *first = TAILQ_FIRST(&queue);
				TAILQ_REMOVE(&queue, first, entries);
				free(first);
			}
		}
		putwchar(*ws);
		if (reading_chorus) {
			*chorus_p++ = *ws;
		}
		ws++;
		j++;
	}
	if (html) {
		wprintf(L"</div>");
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"</div>");
	} else {
		putwchar(L'\n');
		if (reading_chorus)
			*chorus_p++ = L'\n';
	}
}

ssize_t wgetline(wchar_t **line_r, size_t *line_l, FILE *ign) {
	static wchar_t line[BUFSIZ], *s = line;
	ssize_t i = 0;
	memset(line, 0, sizeof(line));
	for (i = 0, s = line; (*s = fgetwc(ign)) != L'\n'; s++, i++) {
		if (*s == L'\0' || *s < 0) {
			*line_l = i;
			*line_r = line;
			return -1;
		}
	}
	s++; i++;
	*line_r = line;
	*line_l = i;
	return i;
}

int main(int argc, char *argv[]) {
	wchar_t *line = NULL;
	char c;
	ssize_t linelen;
	size_t linesize;
	int t = 1;

	chord_db = hash_init();

	while ((c = getopt(argc, argv, "t:hbl")) != -1) switch (c) {
		case 'h':
			  html = 1;
			  break;
		case 'b':
			  bemol = 1;
			  break;
		case 'l':
			  i18n_chord_table = chromatic_latin;
			  break;
		case 't':
			  t = atoi(optarg);
	}

	if (t < 0)
		t += (1 + (t / 12)) * 12;

	setlocale(LC_ALL, "");
	/* setlocale(LC_ALL, "en_US.UTF-8"); */
	hash_table(chord_db, chromatic_en);
	TAILQ_INIT(&queue);

	while ((linelen = wgetline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
