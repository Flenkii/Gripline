#include "game_language.h"
#include "game_addresses.h"
#include "support.h"

int game_language_is_german(unsigned char *game_image) {
    void *manager;
    unsigned char code[32];
    size_t length, capacity;
    if (!game_image || !memory_read(game_image + GAME_RVA_TEXT_TABLES, &manager, sizeof(manager)) ||
        !manager || !memory_read((unsigned char *)manager + 0x30, code, sizeof(code))) {
        return 0;
    }
    memory_copy(&length, code + 16, sizeof(length));
    memory_copy(&capacity, code + 24, sizeof(capacity));
    if (capacity != 15 || length < 2 || length > 3 || code[length] != 0) {
        return 0;
    }
    return text_equal_ignore_case((const char *)code, "DE");
}
