// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whether the first-run setup shows, and the words a stage line uses for a step.

#include "oa/ui/first_run/first_run.hpp"

namespace oa::ui::first_run {

Hold decide(const GateFacts& facts) noexcept {
    if (facts.force)
        return Hold::show;
    if (facts.done)
        return Hold::done;
    if (facts.headless_check)
        return Hold::headless_check;
    if (facts.preferences_file)
        return Hold::preferences_file;
    if (facts.benchmark)
        return Hold::benchmark;
    if (facts.stage)
        return Hold::stage;
    if (facts.fark)
        return Hold::fark;
    if (facts.unattended)
        return Hold::unattended;
    return Hold::show;
}

std::string_view hold_reason(Hold hold) noexcept {
    switch (hold) {
    case Hold::show:
        return {};
    case Hold::done:
        return "done";
    case Hold::headless_check:
        return "headless check";
    case Hold::preferences_file:
        return "preferences file";
    case Hold::benchmark:
        return "benchmark";
    case Hold::stage:
        return "stage";
    case Hold::fark:
        return "fark";
    case Hold::unattended:
        return "unattended";
    }
    return {};
}

std::string_view step_token(Step step) noexcept {
    switch (step) {
    case Step::language:
        return "language";
    case Step::game:
        return "game";
    case Step::copying:
        return "copying";
    case Step::online:
        return "online";
    case Step::buttons:
        return "buttons";
    }
    return "language";
}

std::optional<Step> step_named(std::string_view token) noexcept {
    if (token == "language")
        return Step::language;
    if (token == "game")
        return Step::game;
    if (token == "copying")
        return Step::copying;
    if (token == "online")
        return Step::online;
    if (token == "buttons")
        return Step::buttons;
    return std::nullopt;
}

StepPlace step_place(Step step, Walk walk) noexcept {
    if (walk == Walk::welcome) {
        switch (step) {
        case Step::online:
            return {2, 3, "ONLINE"};
        case Step::buttons:
            return {3, 3, "THE MAIN MENU"};
        case Step::language:
        case Step::game:
        case Step::copying:
            return {1, 3, "WELCOME"};
        }
    }
    switch (step) {
    case Step::language:
        return {1, 5, "LANGUAGE"};
    case Step::game:
        return {2, 5, "THE GAME"};
    case Step::copying:
        return {3, 5, "COPYING"};
    case Step::online:
        return {4, 5, "ONLINE"};
    case Step::buttons:
        return {5, 5, "THE MAIN MENU"};
    }
    return {1, 3, "WELCOME"};
}

bool download_busy(const Model& model) noexcept {
    for (const Card& card : model.cards) {
        if (card.kind != CardKind::pack)
            continue;
        if (card.pack == PackState::downloading || card.pack == PackState::checking)
            return true;
    }
    return false;
}

void bind(Session& session) noexcept {
    const int32_t count = static_cast<int32_t>(session.model.cards.size());
    session.carousel.count = count;
    if (count <= 0) {
        session.carousel.selected = 0;
        return;
    }
    if (session.carousel.selected < 0)
        session.carousel.selected = 0;
    if (session.carousel.selected >= count)
        session.carousel.selected = count - 1;
}

} // namespace oa::ui::first_run
