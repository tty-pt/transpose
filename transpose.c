#include <stddef.h>
#include <err.h>
#include <string.h>
#include <stdlib.h>

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

DB *hash_dbs[HASH_DBS_MAX];

size_t hash_n = 0;

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
int bold = 0, bemol = 0;
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
	static char buf[8], c;
	int not_bolded = 1;

	line[linelen - 1] = '\0';
	if (prev_chord || !*line) {
		prev_chord = 0;
		printf("%s\n", line);
		return;
	}

	prev_chord = 1;

	for (register char *s = line; *s; s++) {
		if (*s == ' ') {
			putchar(' ');
			continue;
		}

		register char
			*eoc = s + (s[1] == '#' || s[1] == 'b' ? 2 : 1),
			 *space_after = strchr(eoc, ' ');

		register size_t chord;

		memset(buf, 0, sizeof(buf));
		strncpy(buf, s, eoc - s);
		chord = SHASH_GET(chord_db, buf);

		if (not_bolded) {
			if (chord == HASH_NOT_FOUND) {
				printf("%s", line);
				break;
			} else if (bold) {
				printf("<b>");
				not_bolded = 0;
			}
		}

		chord = (chord + t) % 12;

		char *new_cstr = chord_str(chord);
		int len = strlen(buf);
		int diff = strlen(new_cstr) - len;
		/* fprintf(stderr, "%s -> %s (%d) EOC: %s\n", buf, new_cstr, diff, eoc); */
		memset(buf, 0, sizeof(buf));

		if (space_after) {
			int odiff = space_after - eoc;
			strncpy(buf, eoc, odiff);
		} else
			strcpy(buf, eoc);

		s = eoc + strlen(buf) - 1;
		int add = diff < 0 ? 1 : -1;
		while (*(s - add) == ' ' && diff) { s -= add; diff += add; }
		if (buf[0] == 'm' && i18n_chord_table == chromatic_latin)
			buf[0] = '-';
		printf("%s%s", new_cstr, buf);
	}

	if (bold && !not_bolded)
		printf("</b>");

	not_bolded = 1;
	putchar('\n');
}

int main(int argc, char *argv[]) {
	char *line = NULL;
	char buf[8], c;
	ssize_t linelen;
	size_t linesize;
	int t = 1;

	chord_db = hash_init();

	while ((c = getopt(argc, argv, "t:Bbl")) != -1) switch (c) {
		case 'B':
			  bold = 1;
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

	hash_table(chord_db, chromatic_en);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
