#include "utils.h"
#include "mygccdef.h"

// Pseudo Random Number - Linear congruential generator
uint8_t prandom(void) {
    static uint16_t Seed = 0;
    Seed = 25173 * Seed + 1;
    return hibyte(Seed);
}