#include "TimelinePane.h"
#include "MainWindow.h"
#include "Project.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QPixmap>

namespace vlip {

namespace {

constexpr int kThumbHeight = 48;
constexpr int kThumbMaxWidth = 160;

} // namespace

TimelinePane::TimelinePane(MainWindow* mw, QWidget* parent) : QWidget(parent), m_mw(mw) {
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout;
    auto* btnImport = new QPushButton(tr("Import…"), this);
    auto* btnRemove = new QPushButton(tr("Remove"), this);
    row->addWidget(btnImport);
    row->addWidget(btnRemove);
    row->addStretch(1);
    vbox->addLayout(row);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QString(), tr("Thumb"), tr("Filename"), tr("Date")});
    m_tree->setRootIsDecorated(false);
    m_tree->setIconSize(QSize(kThumbMaxWidth, kThumbHeight));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setMinimumSectionSize(20);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(0, 28);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    vbox->addWidget(m_tree);

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

    refresh();
}

void TimelinePane::refresh() {
    // Block signals on the tree so setCheckState() / setText() don't fire
    // itemChanged → setUsed feedback. Using QSignalBlocker is more robust
    // than the m_suspendSignals flag (covers signals we forgot to gate).
    QSignalBlocker treeBlock(m_tree);
    m_suspendSignals = true;
    m_tree->clear();
    for (const auto& it : m_mw->project().items) {
        // Build the item detached, configure flags + check state BEFORE
        // attaching to the tree. Avoids a Qt6 quirk where setCheckState()
        // immediately after `new QTreeWidgetItem(parent)` is sometimes
        // ignored visually.
        auto* row = new QTreeWidgetItem;
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(0, it.common().used ? Qt::Checked : Qt::Unchecked);
        row->setData(0, Qt::UserRole, it.common().id.toString());
        if (!it.common().thumbPath.isEmpty()) {
            QPixmap pm(it.common().thumbPath);
            if (!pm.isNull()) {
                pm = pm.scaledToHeight(kThumbHeight, Qt::SmoothTransformation);
                if (pm.width() > kThumbMaxWidth) {
                    pm = pm.scaledToWidth(kThumbMaxWidth, Qt::SmoothTransformation);
                }
                row->setIcon(1, QIcon(pm));
            }
        }
        QString fname = QFileInfo(it.common().sourcePath).fileName();
        if (it.common().sourceMissing) fname += "  [missing]";
        row->setText(2, fname);

        QDateTime ts = it.common().timestamp;
        QString dateText;
        if (ts.isValid()) {
            ts.setTimeSpec(Qt::UTC);
            dateText = ts.toLocalTime().toString("yyyy-MM-dd HH:mm");
        }
        if (it.common().timestampUncertain) dateText += "  ?";
        row->setText(3, dateText);

        if (!it.common().used) {
            QBrush dim(QColor(140, 140, 140));
            for (int c = 0; c < m_tree->columnCount(); ++c) {
                row->setForeground(c, dim);
            }
        }
        if (it.common().sourceMissing) {
            row->setForeground(2, QBrush(QColor(200, 70, 70)));
        }
        m_tree->addTopLevelItem(row);
    }
    QUuid sel = m_mw->selectedId();
    if (!sel.isNull()) selectId(sel);
    // Qt sometimes resizes Fixed columns when items are populated; re-pin.
    m_tree->setColumnWidth(0, 28);
    m_suspendSignals = false;
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
