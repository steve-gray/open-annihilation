// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// An order that watches a unit and is then pointed at another unit, or at
// no unit, leaves the first unit's list. Freeing that order and hitting the
// first unit wakes only the orders that still watch it.
#include "combat_fixture.hpp"
#include "match_tick_access.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;
using sim::match_runtime::MatchTickAccess;

constexpr uint32_t damage_wake = 0x10; // the damage reaction wakes watchers with it

// One order keeps watching the first unit. A second order starts there and
// is then pointed at `other` (null leaves it watching nobody). The second
// order is cancelled, and a hit on the first unit must still wake the order
// that stayed.
void retarget_leaves_the_old_unit(bool other_unit) {
    Options options;
    options.per_player_limit = 8;
    Fixture f(options);
    auto& watcher = f.spawn(0, 32, 64);
    auto& mover = f.spawn(0, 48, 64);
    auto& first = f.spawn(0, 96, 64);
    sim::unit_spawn::Slot* second = nullptr;
    if (other_unit)
        second = &f.spawn(0, 128, 64);

    auto& staying = f.match->issue_guard(watcher.unit_index, first.unit_index, false);
    auto& moving = f.match->issue_guard(mover.unit_index, first.unit_index, false);
    CHECK(!f.match->order_refused(staying) && !f.match->order_refused(moving));
    const auto place = MatchTickAccess::observer_place(*f.match, moving);
    CHECK(place.node != nullptr);
    CHECK(MatchTickAccess::list_holds(*f.match, *first.unit, place));
    const auto staying_place = MatchTickAccess::observer_place(*f.match, staying);
    CHECK(MatchTickAccess::list_holds(*f.match, *first.unit, staying_place));

    MatchTickAccess::link_target(*f.match, moving, second != nullptr ? second->unit : nullptr);
    CHECK(!MatchTickAccess::list_holds(*f.match, *first.unit, place));
    CHECK(MatchTickAccess::list_holds(*f.match, *first.unit, staying_place));
    if (second != nullptr)
        CHECK(MatchTickAccess::list_holds(*f.match, *second->unit, place));
    CHECK(f.match->fault() == nullptr);

    f.match->stop_orders(mover.unit_index);
    CHECK(!MatchTickAccess::list_holds(*f.match, *first.unit, place));
    if (second != nullptr)
        CHECK(!MatchTickAccess::list_holds(*f.match, *second->unit, place));
    f.match->apply_damage_event(first, nullptr, 1, weapon_hit, 0);
    CHECK((staying.raised_events & damage_wake) != 0);
    CHECK(f.match->fault() == nullptr);
}

void retarget_to_another_unit_leaves_the_first() {
    retarget_leaves_the_old_unit(true);
    std::cout << "retarget to another unit leaves the first passed\n";
}

void retarget_to_no_unit_leaves_the_first() {
    retarget_leaves_the_old_unit(false);
    std::cout << "retarget to no unit leaves the first passed\n";
}

} // namespace

int main() {
    try {
        retarget_to_another_unit_leaves_the_first();
        retarget_to_no_unit_leaves_the_first();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
