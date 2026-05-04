#include "Project.hpp"

#include <algorithm>

namespace vlip {

double Item::effectiveDuration() const {
    switch (kind) {
        case ItemKind::ImageClip:
            return std::max(0.0, imageClip.durationSecs);
        case ItemKind::VideoClip: {
            double end = videoClip.endSecs > 0.0 ? videoClip.endSecs : videoClip.sourceDurationSecs;
            double start = std::max(0.0, videoClip.startSecs);
            return std::max(0.0, end - start);
        }
        case ItemKind::TextClip:
            return std::max(0.0, textClip.durationSecs);
    }
    return 0.0;
}

void Project::sortChronologically() {
    std::stable_sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            return a.common().timestamp < b.common().timestamp;
        });
}

int Project::indexOfId(const QUuid& id) const {
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].common().id == id) return i;
    }
    return -1;
}

void Project::applyImageClipDurationAll(double secs) {
    // Bulk write to items only; project defaults intentionally untouched
    // so the bulk action is independent of the Project settings pane.
    for (auto& it : items) {
        if (it.kind == ItemKind::ImageClip) {
            it.imageClip.durationSecs = secs;
        }
    }
}

void Project::applyTextClipDurationAll(double secs) {
    for (auto& it : items) {
        if (it.kind == ItemKind::TextClip) {
            it.textClip.durationSecs = secs;
        }
    }
}

} // namespace vlip
