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
hash_table(int hd, char *table[]) {
	for (size_t i = 0; table[i]; i++) {
		char *str = table[i];
		size_t len = strlen(str);
		hash_put(hd, str, len, i);
		str += len;
		if (*str)
			hash_put(hd, str, strlen(str), i);
	}
}

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
};

static char **i18n_chord_table = chromatic_en;
int chord_db = -1;
int html = 0, bemol = 0;
int prev_chord = 0;

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
	int not_bolded = 1;
	unsigned j = 0;
	unsigned not_chords = 0;

	line[linelen - 1] = '\0';
	if (prev_chord)
		goto no_chord;

	if (skip_empty && !strcmp(line, "")) {
		skip_empty = 0;
		return;
	} else if (!strcmp(line, "-- Chorus")) {
		wprintf(L"%ls", chorus);
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

	for (s = line; *s;) {
		if (*s == ' ') {
			putwchar(' ');
			if (reading_chorus)
				*chorus_p++ = L' ';
			s++;
			j++;
			continue;
		}

		int notflat = s[1] != '#' && s[1] != 'b';

		register char
			*eoc = s + (!notflat ? 2 : 1),
			 *space_after = strchr(eoc, ' ');

		register size_t chord;

		memset(buf, 0, sizeof(buf));
		strncpy(buf, s, eoc - s);
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

		char *new_cstr = chord_str(chord);
		int len = strlen(buf);
		int diff = strlen(new_cstr) - len;
		int modlen, i;

		modlen = space_after ? space_after - eoc : strlen(eoc);
		memset(buf, 0, sizeof(buf));
		strncpy(buf, eoc, modlen);
		j += eoc - s + modlen;
		s = eoc + modlen;
		char *is = s;

		for (i = 0; i < diff && *s == ' '; i++, s++, j++) ;

		if (*s == '\0')
			for (i = 0; i < diff; i++, j++) ;

		if (buf[0] == 'm' && i18n_chord_table == chromatic_latin)
			buf[0] = '-';

		wprintf(L"%s%s", new_cstr, buf);
		if (reading_chorus)
			chorus_p += swprintf(chorus_p, sizeof(chorus) - (chorus_p - chorus), L"%s%s", new_cstr, buf);

		if (*s != ' ' && s + 1 < line + linelen) {
			putwchar(' ');
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

	if (html && s == line) {
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
		putwchar(L'\n');
		if (reading_chorus)
			*chorus_p++ = L'\n';
	}
	return;

no_chord:
	mbstowcs(wline, line, sizeof(wline));
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
					wchar_t c = *(ws - 1) == ' ' ? ' ' : '-';
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

int main(int argc, char *argv[]) {
	char *line = NULL;
	char buf[8], c;
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

	setlocale(LC_ALL, "en_US.UTF-8");
	hash_table(chord_db, chromatic_en);
	TAILQ_INIT(&queue);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
