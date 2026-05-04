#include "TimelinePane.hpp"
#include "MainWindow.hpp"
#include "Project.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QPixmap>

namespace vlip {

namespace {

constexpr int kThumbHeight = 48;
constexpr int kThumbMaxWidth = 160;

// 16:9 thumbnail with a centred "T" — used for text clips, which have no
// source image to derive a thumbnail from.
QPixmap textClipThumbnail() {
    static QPixmap cached;
    if (!cached.isNull()) return cached;
    int w = kThumbHeight * 16 / 9;
    QPixmap pm(w, kThumbHeight);
    pm.fill(QColor(35, 35, 45));
    QPainter p(&pm);
    p.setPen(QColor(220, 220, 220));
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(int(kThumbHeight * 0.62));
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, "T");
    p.setPen(QColor(80, 80, 100));
    p.drawRect(0, 0, w - 1, kThumbHeight - 1);
    cached = pm;
    return cached;
}

QString textClipDisplayName(const QString& text) {
    QString s = text.simplified();
    if (s.isEmpty()) return QObject::tr("(text clip)");
    if (s.size() > 40) s = s.left(37) + "…";
    return s;
}

} // namespace

TimelinePane::TimelinePane(MainWindow* mw, QWidget* parent) : QWidget(parent), m_mw(mw) {
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);

    {
        QSettings s("vlip", "vlip");
        m_hideUnused = s.value("timeline/hideUnused", false).toBool();
    }

    auto* row = new QHBoxLayout;
    auto* btnImport = new QPushButton(tr("Import…"), this);
    auto* btnInsertText = new QPushButton(tr("Insert text clip  "), this);
    btnInsertText->setToolTip(tr(
        "Insert a text clip before or after the currently selected item."));
    auto* textMenu = new QMenu(btnInsertText);
    auto* aTextBefore = textMenu->addAction(tr("Before selected item"));
    auto* aTextAfter  = textMenu->addAction(tr("After selected item"));
    btnInsertText->setMenu(textMenu);
    auto* btnRemove = new QPushButton(tr("Remove"), this);
    m_chkHideUnused = new QCheckBox(tr("Hide unused"), this);
    m_chkHideUnused->setToolTip(tr(
        "Show only items marked as 'used'. Navigation shortcuts skip hidden items."));
    m_chkHideUnused->setChecked(m_hideUnused);
    row->addWidget(btnImport);
    row->addWidget(btnInsertText);
    row->addWidget(btnRemove);
    row->addStretch(1);
    row->addWidget(m_chkHideUnused);
    vbox->addLayout(row);

    connect(m_chkHideUnused, &QCheckBox::toggled, this, [this](bool on) {
        setHideUnused(on);
    });

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QString(), tr("Thumb"), tr("Filename"), tr("Date")});
    m_tree->setRootIsDecorated(false);
    m_tree->setIconSize(QSize(kThumbMaxWidth, kThumbHeight));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    // Columns: ✓ stays fixed-width (it's just a checkbox), the rest are
    // Interactive so the user can drag dividers. Last-section stretching
    // is off so users have full control over every column's width.
    m_tree->header()->setMinimumSectionSize(20);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 28);
    m_tree->setColumnWidth(1, 110);   // thumb
    m_tree->setColumnWidth(2, 240);   // filename
    m_tree->setColumnWidth(3, 130);   // date
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    vbox->addWidget(m_tree);

    // Status line below the tree: count of used items + total render
    // duration. Updated on every refresh / refreshRow.
    m_summary = new QLabel(this);
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_summary->setContentsMargins(4, 2, 4, 2);
    vbox->addWidget(m_summary);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &TimelinePane::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &TimelinePane::onItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &TimelinePane::onContextMenu);

    connect(btnImport, &QPushButton::clicked, this, [this]() {
        QStringList exts;
        exts << "*.jpg" << "*.jpeg" << "*.png" << "*.heic" << "*.heif"
             << "*.webp" << "*.tif" << "*.tiff"
             << "*.mp4" << "*.mov" << "*.m4v" << "*.mkv" << "*.webm" << "*.avi";
        auto paths = QFileDialog::getOpenFileNames(
            this, tr("Import media"),
            QString(),
            tr("Media (%1);;All files (*.*)").arg(exts.join(' ')));
        if (!paths.isEmpty()) m_mw->importPaths(paths);
    });
    connect(btnRemove, &QPushButton::clicked, this, [this]() {
        auto* it = m_tree->currentItem();
        if (!it) return;
        QUuid id = QUuid::fromString(it->data(0, Qt::UserRole).toString());
        m_mw->removeItem(id);
    });
    auto currentRowId = [this]() {
        QUuid id;
        if (auto* cur = m_tree->currentItem()) {
            id = QUuid::fromString(cur->data(0, Qt::UserRole).toString());
        }
        return id;
    };
    connect(aTextBefore, &QAction::triggered, this, [this, currentRowId]() {
        m_mw->addTextClip(currentRowId(), MainWindow::InsertPosition::Before);
    });
    connect(aTextAfter, &QAction::triggered, this, [this, currentRowId]() {
        m_mw->addTextClip(currentRowId(), MainWindow::InsertPosition::After);
    });

    refresh();
}

QIcon TimelinePane::iconForItem(const Item& it) {
    // Text clips share one icon; cache the QIcon once.
    if (it.kind == ItemKind::TextClip) {
        static const QString kKey = QStringLiteral("__textclip__");
        auto cached = m_iconCache.constFind(kKey);
        if (cached != m_iconCache.constEnd()) return *cached;
        QIcon icon(textClipThumbnail());
        m_iconCache.insert(kKey, icon);
        return icon;
    }
    const QString& key = it.common().thumbPath;
    if (key.isEmpty()) return {};
    auto cached = m_iconCache.constFind(key);
    if (cached != m_iconCache.constEnd()) return *cached;
    QPixmap pm(key);
    if (pm.isNull()) {
        // Cache the empty result too so we don't keep retrying broken paths.
        m_iconCache.insert(key, QIcon());
        return {};
    }
    pm = pm.scaledToHeight(kThumbHeight, Qt::SmoothTransformation);
    if (pm.width() > kThumbMaxWidth) {
        pm = pm.scaledToWidth(kThumbMaxWidth, Qt::SmoothTransformation);
    }
    QIcon icon(pm);
    m_iconCache.insert(key, icon);
    return icon;
}

void TimelinePane::populateRow(QTreeWidgetItem* row, const Item& it) {
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    row->setCheckState(0, it.common().used ? Qt::Checked : Qt::Unchecked);
    row->setData(0, Qt::UserRole, it.common().id.toString());
    row->setIcon(1, iconForItem(it));

    QString fname;
    if (it.kind == ItemKind::TextClip) {
        fname = textClipDisplayName(it.textClip.text);
    } else {
        fname = QFileInfo(it.common().sourcePath).fileName();
        if (it.common().sourceMissing) fname += "  [missing]";
    }
    row->setText(2, fname);

    QDateTime ts = it.common().timestamp;
    QString dateText;
    if (ts.isValid()) {
        ts.setTimeSpec(Qt::UTC);
        dateText = ts.toLocalTime().toString("yyyy-MM-dd HH:mm");
    }
    if (it.common().timestampUncertain) dateText += "  ?";
    row->setText(3, dateText);

    // Always set the foreground explicitly so a re-populate (refreshRow)
    // doesn't leave a stale dimmed brush from a prior state.
    QBrush base = (it.common().used) ? QBrush() : QBrush(QColor(140, 140, 140));
    for (int c = 0; c < m_tree->columnCount(); ++c) {
        row->setForeground(c, base);
    }
    if (it.common().sourceMissing) {
        row->setForeground(2, QBrush(QColor(200, 70, 70)));
    }
}

QTreeWidgetItem* TimelinePane::findRow(const QUuid& id) const {
    const QString s = id.toString();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* it = m_tree->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == s) return it;
    }
    return nullptr;
}

void TimelinePane::refresh() {
    // Block signals on the tree so setCheckState() / setText() don't fire
    // itemChanged → setUsed feedback.
    QSignalBlocker treeBlock(m_tree);
    m_suspendSignals = true;
    m_tree->clear();
    for (const auto& it : m_mw->project().items) {
        if (m_hideUnused && !it.common().used) continue;
        // Build the item detached, configure flags + check state BEFORE
        // attaching to the tree. Avoids a Qt6 quirk where setCheckState()
        // immediately after `new QTreeWidgetItem(parent)` is sometimes
        // ignored visually.
        auto* row = new QTreeWidgetItem;
        populateRow(row, it);
        m_tree->addTopLevelItem(row);
    }
    QUuid sel = m_mw->selectedId();
    if (!sel.isNull()) selectId(sel);
    // Qt sometimes resizes Fixed columns when items are populated; re-pin.
    m_tree->setColumnWidth(0, 28);
    m_suspendSignals = false;
    updateSummary();
}

void TimelinePane::refreshRow(const QUuid& id) {
    int idx = m_mw->project().indexOfId(id);
    auto* row = findRow(id);
    if (idx < 0) {
        // Item gone from the project — drop the row if present.
        if (row) {
            int pos = m_tree->indexOfTopLevelItem(row);
            delete m_tree->takeTopLevelItem(pos);
        }
        updateSummary();
        return;
    }
    const Item& it = m_mw->project().items[idx];
    bool shouldBeVisible = !(m_hideUnused && !it.common().used);
    if (!shouldBeVisible) {
        // The item was just toggled off and the filter is on — remove it
        // from the tree.
        if (row) {
            int pos = m_tree->indexOfTopLevelItem(row);
            delete m_tree->takeTopLevelItem(pos);
        }
        updateSummary();
        return;
    }
    if (!row) {
        // Item is supposed to be visible but isn't in the tree (typically
        // it was hidden, then re-marked used). Fall back to a full refresh
        // — re-inserting at the right position is what refresh() does.
        refresh();
        return;
    }
    QSignalBlocker treeBlock(m_tree);
    m_suspendSignals = true;
    populateRow(row, it);
    m_suspendSignals = false;
    updateSummary();
}

void TimelinePane::selectId(const QUuid& id) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* it = m_tree->topLevelItem(i);
        if (QUuid::fromString(it->data(0, Qt::UserRole).toString()) == id) {
            m_tree->setCurrentItem(it);
            return;
        }
    }
}

void TimelinePane::onSelectionChanged() {
    if (m_suspendSignals) return;
    auto* it = m_tree->currentItem();
    if (!it) return;
    QUuid id = QUuid::fromString(it->data(0, Qt::UserRole).toString());
    m_mw->setSelected(id);
}

void TimelinePane::onItemChanged(QTreeWidgetItem* it, int col) {
    if (m_suspendSignals) return;
    if (col != 0) return;
    QUuid id = QUuid::fromString(it->data(0, Qt::UserRole).toString());
    bool used = it->checkState(0) == Qt::Checked;
    m_mw->setUsed(id, used);
}

void TimelinePane::setHideUnused(bool on) {
    if (m_hideUnused == on) return;
    m_hideUnused = on;
    QSettings("vlip", "vlip").setValue("timeline/hideUnused", on);
    if (m_chkHideUnused && m_chkHideUnused->isChecked() != on) {
        QSignalBlocker block(m_chkHideUnused);
        m_chkHideUnused->setChecked(on);
    }
    refresh();
}

void TimelinePane::updateSummary() {
    int usedCount = 0;
    double totalSecs = 0.0;
    for (const auto& it : m_mw->project().items) {
        if (!it.common().used) continue;
        usedCount++;
        totalSecs += it.effectiveDuration();
    }
    int total = int(std::round(totalSecs));
    int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    QString durText = (h > 0)
        ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    int totalCount = m_mw->project().items.size();
    m_summary->setText(tr("%1 of %2 items used  ·  total %3")
        .arg(usedCount).arg(totalCount).arg(durText));
}

QByteArray TimelinePane::saveHeaderState() const {
    return m_tree->header()->saveState();
}

void TimelinePane::restoreHeaderState(const QByteArray& state) {
    if (state.isEmpty()) return;
    m_tree->header()->restoreState(state);
    // The checkbox column should always stay fixed at 28 px no matter
    // what the saved state had; restore can otherwise leave it stale.
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_tree->setColumnWidth(0, 28);
}

void TimelinePane::onContextMenu(const QPoint& pt) {
    auto* it = m_tree->itemAt(pt);
    if (!it) return;
    QUuid id = QUuid::fromString(it->data(0, Qt::UserRole).toString());
    QMenu menu(this);
    auto* aRemove = menu.addAction(tr("Remove from project"));
    auto* aUse = menu.addAction(tr("Toggle 'used'"));
    QAction* picked = menu.exec(m_tree->viewport()->mapToGlobal(pt));
    if (picked == aRemove) m_mw->removeItem(id);
    else if (picked == aUse) {
        bool now = it->checkState(0) == Qt::Checked;
        m_mw->setUsed(id, !now);
    }
}

} // namespace vlip
