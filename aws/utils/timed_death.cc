#include "timed_death.h"

#include <random>

#include <aws/utils/logging.h>

namespace {

time_t die_after = 0;
time_t base_time = 0;
time_t random_offset = 0;
time_t init_time = 0;

volatile time_t* current = 0;

}

namespace aws {
namespace utils {

void TimedDeath::initialize(time_t min_wait)
{

    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<int> uniform(1, min_wait);
    random_offset = uniform(mt);
    time_t now = time(nullptr);
    die_after = now + min_wait + random_offset;

    LOG(info) << "now: " << now << " + random: " << random_offset << " = die_after: " << die_after << " ( " << (die_after - now) << " seconds )";
    init_time = now;
}

void TimedDeath::die_if_time()
{
    time_t now = time(nullptr);
    if (now > die_after) {
        time_t taken = now - init_time;
        LOG(info) << "Time to die: " << now << " > " << die_after << " taken: " << taken << " seconds.";
        *current = now;
    }
}

}
}
