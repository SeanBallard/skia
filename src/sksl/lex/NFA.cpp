/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "NFA.h"

int NFA::match(std::string s) const {
    std::vector<int> states = fStartStates;
    for (size_t i = 0; i < s.size(); ++i) {
        std::vector<int> next;
        for (int id : states) {
            if (fStates[id].accept(s[i])) {
                for (int nextId : fStates[id].fNext) {
                    if (fStates[nextId].fKind != NFAState::kRemapped_Kind) {
                        next.push_back(nextId);
                    } else {
                        next.insert(next.end(), fStates[nextId].fData.begin(),
                                    fStates[nextId].fData.end());
                    }
                }
            }
        }
        if (!next.size()) {
            return -1;
        }
        states = next;
    }
    int accept = -1;
    for (int id : states) {
        if (fStates[id].fKind == NFAState::kAccept_Kind) {
            int result = fStates[id].fData[0];
            if (accept == -1 || result < accept) {
                accept = result;
            }
        }
    }
    return accept;
}
