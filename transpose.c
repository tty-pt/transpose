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
	for (size_t i = 0; table[i]; i++)
		hash_put(hd, table[i], strlen(table[i]), i);
}

static char *chord_table[] = {
	"C",
	"Cm",
	"C#",
	"C#m",
	"D",
	"Dm",
	"D#m",
	"E",
	"Em",
	"F",
	"Fm",
	"F#",
	"F#m",
	"G",
	"Gm",
	"G#m",
	"A",
	"Am",
	"A#",
	"A#m",
	"B",
	"Bm",
	NULL,
};

int chord_db = -1;
int bold = 0;

static inline void
proc_line(char *line, size_t linelen, int t)
{
	static char buf[8], c;
	int not_bolded = 1;

	line[linelen - 1] = '\0';
	for (register char *s = line; *s; s++) {
		if (*s == ' ') {
			putchar(' ');
			continue;
		}

		register char *space_after = strchr(s, ' ');
		register size_t chord;

		if (space_after) {
			memset(buf, 0, sizeof(buf));
			strncpy(buf, s, space_after - s);
			chord = SHASH_GET(chord_db, buf);
			s = space_after - 1;
		} else {
			chord = SHASH_GET(chord_db, s);
		}

		if (chord == HASH_NOT_FOUND) {
			printf("%s", line);
			return;
		} else if (bold && not_bolded) {
			printf("<b>");
			not_bolded = 0;
		}

		chord = (chord + t) % 22;
		printf("%s", chord_table[chord]);
	}

	if (!not_bolded)
		printf("</b>");

	putchar('\n');
}

int main(int argc, char *argv[]) {
	char *line = NULL;
	char buf[8], c;
	ssize_t linelen;
	size_t linesize;
	int t = 1;

	chord_db = hash_init();

	while ((c = getopt(argc, argv, "t:b")) != -1) switch (c) {
		case 'b':
			  bold = 1;
			  break;
		case 't':
			  t = atoi(optarg);
	}


	hash_table(chord_db, chord_table);

	while ((linelen = getline(&line, &linesize, stdin)) >= 0)
		proc_line(line, linelen, t);
}
