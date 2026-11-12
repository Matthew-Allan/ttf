#include<stdio.h>
#include<stdlib.h>
#include<stddef.h>
#include<inttypes.h>
#include<limits.h>

#define DEBUG

#ifdef DEBUG
#define debugf(...) printf(__VA_ARGS__)
#else
#define debugf(...)
#endif

#define ERR 1
#define OK  0

#define CLEANUP_GT cleanup

#define HANDLE(code) do { if (code) return ERR; } while (0)
#define HANDLE_OUT(code, ...) do { if (code) { fprintf(stderr, __VA_ARGS__); return ERR; } } while(0)
#define HANDLE_GT(code) do { if (code) goto CLEANUP_GT; } while (0)
#define HANDLE_OUT_GT(code, ...) do { if (code) { fprintf(stderr, __VA_ARGS__); goto CLEANUP_GT; } } while(0)

// Doesn't work well for multiple cleanups.
#define INIT_CLEANUP() int __res = ERR;
#define CLEANUP(code) do { __res = OK; CLEANUP_GT: do { code; } while(0); } while(0)
#define RESULT() __res

/* All recognised table tags as 32bit ints. */
enum tableTags {
    CMAP_TAG = 0x636d6170, // `cmap`
    GLYF_TAG = 0x676c7966, // `glyf`
    HEAD_TAG = 0x68656164, // `head`
    HHEA_TAG = 0x68686561, // `hhea`
    HMTX_TAG = 0x686d7478, // `hmtx`
    LOCA_TAG = 0x6c6f6361, // `loca`
    MAXP_TAG = 0x6d617870, // `maxp`
    NAME_TAG = 0x6e616d65, // `name`
    POST_TAG = 0x706f7374  // `post`
};

/* The main header found at off:0 in a `.ttf` file. */
struct offsetSubtable {
    uint32_t scalar_type;       // The type of the TTF file TODO: check this is valid.
    uint16_t num_tables;        // The number of tables in the directory.
    uint16_t search_range;      // TODO: work out what this is.
    uint16_t entry_selector;    // TODO: work out what this is.
    uint16_t range_shift;       // TODO: work out what this is.
};
#define OFFSET_SUBTABLE_SIZE 12 /* Size of `offsetSubtable` on file. */

/* An entry found after the offset subtable containing information regarding a particular table. */
struct tableDirectoryEntry {
    uint8_t tag[4];     // The identifier for the table.
    uint32_t checksum;  // A checksum TODO: check that the checksum is valid.
    uint32_t offset;    // The offset to the table.
    uint32_t length;    // The length in bytes of the table.
};
#define TABLE_DIRECTORY_ENTRY_SIZE 16 /* Size of `tableDirectoryEntry` on file. */

/* The header for the `cmap` table which contains the character-to-glyph mapping information. */
struct cmapTable {
    uint16_t version;       // TODO: work out what this is.
    uint16_t num_subtables; // Number of cmap subtables in the table.
};
#define CMAP_TABLE_SIZE 4 /* Size of `cmapTable` on file. */

/* The header for the `cmap` table which contains the character-to-glyph mapping information. */
struct cmapSubtable {
    uint16_t platform_id;           // TODO: work out what this is.
    uint16_t platform_specific_id;  // Number of cmap subtables in the table.
    uint32_t offset;                // Offset of the mapping table
};
#define CMAP_SUBTABLE_SIZE 8 /* Size of `cmapSubtable` on file. */

/* The header for the `name` table which contains human-readable strings for features and settings. */
struct nameTable {
    uint16_t format;        // TODO: work out what this is.
    uint16_t count;         // The number of entries.
    uint16_t string_offset; // The offset to the start of the name character strings from `tableDirectoryEntry.offset` in bytes.
};
#define NAME_TABLE_SIZE 6 /* Size of `nameTable` on file. */

/* A record in the name table describing a particular string. */
struct nameRecord {
    uint16_t platform_id;           // ID code for the target platform TODO: check valid & utilise.
    uint16_t platform_specific_id;  // TODO: work out what this is.
    uint16_t language_id;           // TODO: work out what this is.
    uint16_t name_id;               // TODO: work out what this is.
    uint16_t length;                // The length of the string in bytes.
    uint16_t offset;                // The offset of the string from `nameTable.string_offset` in bytes.
};
#define NAME_RECORD_SIZE 12 /* Size of `nameRecord` on file. */

/* Read `size` bytes into a buffer from a file. */
static inline int readBuff(FILE *file, uint8_t *buff, size_t size) {
    HANDLE_OUT(fread(buff, 1, size, file) != size, "Failed to read %zu bytes from the file.\n", size);
    return OK;
}

/* Turn two bytes into one uint16_t value. Assumes big-endian. */
static inline uint16_t buffTo16(const uint8_t buff[2]) {
    return (uint16_t) buff[1] | ((uint16_t) buff[0] << 8);
}

/* Turn four bytes into one uint32_t value. Assumes big-endian. */
static inline uint32_t buffTo32(const uint8_t buff[4]) {
    return (uint32_t) buff[3] | ((uint32_t) buff[2] << 8) | ((uint32_t) buff[1] << 16) | ((uint32_t) buff[0] << 24);
}

/* Read a (big-endian) uint16_t from a file. */
static int read16(FILE *file, uint16_t *out) {
    uint8_t buff[2];
    HANDLE(readBuff(file, buff, 2));
    *out = buffTo16(buff);
    return OK;
}

/* Read a (big-endian) uint32_t from a file. */
static int read32(FILE *file, uint32_t *out) {
    uint8_t buff[4];
    HANDLE(readBuff(file, buff, 4));
    *out = buffTo32(buff);
    return OK;
}

/* Read a TTF offset subtable struct from a file. */
static int readOffsetSubtable(FILE *file, struct offsetSubtable *offset_subtable) {
    uint8_t buff[OFFSET_SUBTABLE_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, OFFSET_SUBTABLE_SIZE));
    offset_subtable->scalar_type    = buffTo32(ptr); ptr += 4;
    offset_subtable->num_tables     = buffTo16(ptr); ptr += 2;
    offset_subtable->search_range   = buffTo16(ptr); ptr += 2;
    offset_subtable->entry_selector = buffTo16(ptr); ptr += 2;
    offset_subtable->range_shift    = buffTo16(ptr); ptr += 2;
    return OK;
}

/* Read a TTF table directory struct from a file. */
static int readTableDirectoryEntry(FILE *file, struct tableDirectoryEntry *entry) {
    uint8_t buff[TABLE_DIRECTORY_ENTRY_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, TABLE_DIRECTORY_ENTRY_SIZE));
    for (int i = 0; i < 4; i++) entry->tag[i] = *(ptr++);
    entry->checksum = buffTo32(ptr); ptr += 4;
    entry->offset   = buffTo32(ptr); ptr += 4;
    entry->length   = buffTo32(ptr); ptr += 4;
    for (int i = 0; i < 4; i++) {
        HANDLE_OUT(entry->tag[i] < 32 || entry->tag[i] > 126, "Encountered a table tag that was unprintable: 0x%08"PRIx32".\n", buffTo32(entry->tag));
    }
    return OK;
}

/* Read a `cmap` table struct from a file. */
static int readCmapTable(FILE *file, struct cmapTable *table) {
    uint8_t buff[CMAP_TABLE_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, CMAP_TABLE_SIZE));
    table->version          = buffTo16(ptr); ptr += 2;
    table->num_subtables    = buffTo16(ptr); ptr += 2;
    return OK;
}

/* Read a `cmap` table's subtable struct from a file. */
static int readCmapSubtable(FILE *file, struct cmapSubtable *subtable) {
    uint8_t buff[CMAP_SUBTABLE_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, CMAP_SUBTABLE_SIZE));
    subtable->platform_id           = buffTo16(ptr); ptr += 2;
    subtable->platform_specific_id  = buffTo16(ptr); ptr += 2;
    subtable->offset                = buffTo32(ptr); ptr += 4;
    return OK;
}

/* Read a `name` table struct from a file. */
static int readNameTable(FILE *file, struct nameTable *table) {
    uint8_t buff[NAME_TABLE_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, NAME_TABLE_SIZE));
    table->format           = buffTo16(ptr); ptr += 2;
    table->count            = buffTo16(ptr); ptr += 2;
    table->string_offset    = buffTo16(ptr); ptr += 2;
    return OK;
}

/* Read a `name` table's record struct from a file. */
static int readNameRecord(FILE *file, struct nameRecord *record) {
    uint8_t buff[NAME_RECORD_SIZE], *ptr = buff;
    HANDLE(readBuff(file, buff, NAME_RECORD_SIZE));
    record->platform_id             = buffTo16(ptr); ptr += 2;
    record->platform_specific_id    = buffTo16(ptr); ptr += 2;
    record->language_id             = buffTo16(ptr); ptr += 2;
    record->name_id                 = buffTo16(ptr); ptr += 2;
    record->length                  = buffTo16(ptr); ptr += 2;
    record->offset                  = buffTo16(ptr); ptr += 2;
    return OK;
}

/* Read a TTF `cmap` table and process accordingly. */
static int parseCmapTable(FILE *file, const struct tableDirectoryEntry *entry) {
    struct cmapTable table;
    HANDLE(readCmapTable(file, &table));
    debugf("0x%04"PRIx16" count:%"PRIu16"\n", table.version, table.num_subtables);
    struct cmapSubtable subtable; // Don't need to store all as only one needs to be processed.
    for (int i = 0; i < table.num_subtables; i++) {
        HANDLE(readCmapSubtable(file, &subtable));
        debugf(" - SUBTABLE plat:[%"PRIu16":%"PRIu16"] off:%"PRIu32"\n",
            subtable.platform_id, subtable.platform_specific_id,
            subtable.offset
        );
    }
    return OK;
}

/* Read a TTF `glyf` table and process accordingly. */
static int parseGlyfTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'glyf' is unimplemented.\n");
    return OK;
}

/* Read a TTF `head` table and process accordingly. */
static int parseHeadTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'head' is unimplemented.\n");
    return OK;
}

/* Read a TTF `hhea` table and process accordingly. */
static int parseHheaTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'hhea' is unimplemented.\n");
    return OK;
}

/* Read a TTF `hmtx` table and process accordingly. */
static int parseHmtxTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'hmtx' is unimplemented.\n");
    return OK;
}

/* Read a TTF `loca` table and process accordingly. */
static int parseLocaTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'loca' is unimplemented.\n");
    return OK;
}

/* Read a TTF `maxp` table and process accordingly. */
static int parseMaxpTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'maxp' is unimplemented.\n");
    return OK;
}

/* Read a TTF `name` table's record and process accordingly. */
static int parseNameTableRecord(FILE *file, const struct tableDirectoryEntry *entry, const uint16_t string_offset, const struct nameRecord *record) {
    // Check for now only allows support of UTF-16BE.
    HANDLE_OUT(record->length % 2, "Record in 'name' expected to be UTF-16BE, should be a multiple of 2 bytes.\n");
    uint16_t chars = record->length / 2;
    char ascii[chars + 1];

    uint64_t offset = (uint64_t) entry->offset + string_offset + record->offset;
    HANDLE_OUT(offset > LONG_MAX, "Unsupported file offset.\n");
    HANDLE(fseek(file, (long) offset, SEEK_SET));

    uint8_t *utf_16be = NULL;
    INIT_CLEANUP();
    if (record->length) {
        utf_16be = malloc(record->length);
        HANDLE_OUT(utf_16be == NULL, "Failed to allocate memory for 'name' table record value.\n");
        HANDLE_GT(readBuff(file, utf_16be, record->length));
        for (int j = 0; j < chars; j++) {
            uint16_t chr = buffTo16(utf_16be + j * 2);
            ascii[j] = chr > 126 || chr < 32 ? '?' : (char) chr;
        }
    }
    ascii[chars] = '\0';
    debugf(" - RECORD plat:[%"PRIu16":%"PRIu16"] lang:%"PRIu16" id:%"PRIu16" [%"PRIu16"->%"PRIu16"]: '%s'\n",
        record->platform_id, record->platform_specific_id,
        record->language_id, record->name_id,
        record->offset,      record->length,
        ascii
    );
    CLEANUP(free(utf_16be));
    return RESULT();
}

/* Read a TTF `name` table and process accordingly. */
static int parseNameTable(FILE *file, const struct tableDirectoryEntry *entry) {
    struct nameTable table;
    HANDLE(readNameTable(file, &table));
    debugf("0x%04"PRIx16" off:%"PRIu16" count:%"PRIu16"\n", table.format, table.string_offset, table.count);
    if (table.count == 0) return OK;
    HANDLE_OUT(NAME_TABLE_SIZE + table.count * NAME_RECORD_SIZE > entry->length, "Name table size misrepresented by table directory entry.\n");
    // TODO: check string_offset is not before end of head + record.
    // TODO: check total size including struings doesn't exceed table length (probably per record).
    
    INIT_CLEANUP();
    struct nameRecord *const records = malloc(sizeof(struct nameRecord) * table.count), *record = records;
    HANDLE_OUT(records == NULL, "Failed to allocate memory for 'name' table records.\n");
    for (int i = 0; i < table.count; i++, record++) {
        HANDLE_GT(readNameRecord(file, record));
    }
    record = records;
    for (int i = 0; i < table.count; i++, record++) {
        HANDLE_GT(parseNameTableRecord(file, entry, table.string_offset, record));
    }
    CLEANUP(free(records));
    return RESULT();
}

/* Read a TTF `post` table and process accordingly. */
static int parsePostTable(FILE *file, const struct tableDirectoryEntry *entry) {
    debugf("'post' is unimplemented.\n");
    return OK;
}

/* Parse the tables inside the TTF file. */
static int parseTables(FILE *file) {
    struct offsetSubtable offset_subtable;
    HANDLE(readOffsetSubtable(file, &offset_subtable));
    debugf("OST [0x%08"PRIx32"] lookup{rng:%"PRIu16" slr:%"PRIu16" sft:%"PRIu16"} tables:%"PRIu16"\n",
        offset_subtable.scalar_type,    offset_subtable.search_range,
        offset_subtable.entry_selector, offset_subtable.range_shift,
        offset_subtable.num_tables
    );
    HANDLE_OUT(offset_subtable.num_tables == 0, "File lists no tables.\n");

    INIT_CLEANUP();
    struct tableDirectoryEntry *const entries = malloc(sizeof(struct tableDirectoryEntry) * offset_subtable.num_tables), *entry = entries;
    HANDLE_OUT(entries == NULL, "Failed to allocate memory for table directory entries.\n");
    for (int i = 0; i < offset_subtable.num_tables; i++, entry++) {
        HANDLE_GT(readTableDirectoryEntry(file, entry));
    }
    entry = entries;
    for (int i = 0; i < offset_subtable.num_tables; i++, entry++) {
        HANDLE_OUT_GT(entry->offset > LONG_MAX, "Unsupported file offset.\n");
        HANDLE_GT(fseek(file, entry->offset, SEEK_SET));
        debugf("TABLE '%.4s' 0x%08"PRIx32" [%"PRIu32"->%"PRIu32"] : ", entry->tag, entry->checksum, entry->offset, entry->length);
        switch (buffTo32(entry->tag)) {
            case CMAP_TAG: HANDLE_GT(parseCmapTable(file, entry)); break;
            case GLYF_TAG: HANDLE_GT(parseGlyfTable(file, entry)); break;
            case HEAD_TAG: HANDLE_GT(parseHeadTable(file, entry)); break;
            case HHEA_TAG: HANDLE_GT(parseHheaTable(file, entry)); break;
            case HMTX_TAG: HANDLE_GT(parseHmtxTable(file, entry)); break;
            case LOCA_TAG: HANDLE_GT(parseLocaTable(file, entry)); break;
            case MAXP_TAG: HANDLE_GT(parseMaxpTable(file, entry)); break;
            case NAME_TAG: HANDLE_GT(parseNameTable(file, entry)); break;
            case POST_TAG: HANDLE_GT(parsePostTable(file, entry)); break;
            default: debugf("Unrecognised\n"); break;
        }
    }
    CLEANUP(free(entries));
    return RESULT();
}

/**
 * Parses the contents of a `.ttf` file.
 * 
 * @param file_path The TrueType font file to be parsed.
 * @return On success 0; on failure 1.
 */
int parseTTF(const char *file_path) {
    INIT_CLEANUP();
    FILE *file = fopen(file_path, "rb");
    HANDLE_OUT(file == NULL, "Could not open file '%s'.\n", file_path);
    HANDLE_GT(parseTables(file));
    CLEANUP(HANDLE_OUT(fclose(file), "Failed to close file '%s'.\n", file_path));
    return RESULT();
}

int main(int argc, char const *argv[]) {
    HANDLE_OUT(argc != 2, "Invalid arguments: expected exactly one argument (ttf file path).\n");
    return parseTTF(argv[1]);
}
