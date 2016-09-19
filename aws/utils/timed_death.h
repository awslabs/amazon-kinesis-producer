#ifndef TIMED_DEATH_H
#define TIMED_DEATH_H

#include <time.h>


namespace aws {
namespace utils {
class TimedDeath
{
private:
    TimedDeath() = default;
public:

    static void initialize(time_t min_wait);
    static void die_if_time();

};

}
}

#endif // TIMEDDEATH_H
