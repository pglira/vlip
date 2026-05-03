#include "Project.h"

#include <algorithm>

namespace vlip {

double Item::effectiveDuration() const {
    switch (kind) {
        case ItemKind::Image:
            return std::max(0.0, image.durationSecs);
        case ItemKind::Video: {
            double end = video.endSecs > 0.0 ? video.endSecs : video.sourceDurationSecs;
            double start = std::max(0.0, video.startSecs);
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

void Project::applyImageDurationAll(double secs) {
    defaults.imageDuration = secs;
    for (auto& it : items) {
        if (it.kind == ItemKind::Image) {
            it.image.durationSecs = secs;
        }
    }
}

void Project::applyTextClipDurationAll(double secs) {
    defaults.textClip.defaultDuration = secs;
    for (auto& it : items) {
        if (it.kind == ItemKind::TextClip) {
            it.textClip.durationSecs = secs;
        }
    }
}

} // namespace vlip
