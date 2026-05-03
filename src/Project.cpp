#include "Project.h"

#include <algorithm>

namespace vlip {

double Item::effectiveDuration() const {
    if (kind == ItemKind::Image) {
        return std::max(0.0, image.durationSecs);
    }
    double end = video.endSecs > 0.0 ? video.endSecs : video.sourceDurationSecs;
    double start = std::max(0.0, video.startSecs);
    return std::max(0.0, end - start);
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

} // namespace vlip
