#include "PreviewPane.h"
#include "MainWindow.h"
#include "ImagePreviewWidget.h"
#include "VideoPreviewWidget.h"
#include "Project.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>

namespace vlip {

PreviewPane::PreviewPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_subtitle = new QLineEdit(this);
    m_subtitle->setPlaceholderText(tr("Subtitle"));
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setEnabled(false);
    v->addWidget(m_subtitle);

    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack, 1);

    m_image = new ImagePreviewWidget(this);
    m_video = new VideoPreviewWidget(mw, this);
    auto* placeholder = new QLabel(tr("(select an item)"), this);
    placeholder->setAlignment(Qt::AlignCenter);

    m_stack->addWidget(m_image);       // 0
    m_stack->addWidget(m_video);       // 1
    m_stack->addWidget(placeholder);   // 2

    connect(m_subtitle, &QLineEdit::editingFinished, this, [this]() {
        if (m_suspend || m_id.isNull()) return;
        m_mw->setSubtitle(m_id, m_subtitle->text());
    });

    refresh();
}

void PreviewPane::onSelectionChanged(const QUuid& id) {
    m_id = id;
    refresh();
}

void PreviewPane::refresh() {
    int idx = m_mw->project().indexOfId(m_id);
    m_suspend = true;
    if (idx < 0) {
        m_image->clear();
        m_video->clear();
        m_subtitle->clear();
        m_subtitle->setEnabled(false);
        m_stack->setCurrentIndex(2);
        m_suspend = false;
        return;
    }
    const Item& it = m_mw->project().items[idx];
    m_subtitle->setText(it.common().subtitle);
    m_subtitle->setEnabled(!it.common().sourceMissing);
    if (it.common().sourceMissing) {
        m_image->clear();
        m_video->clear();
        m_stack->setCurrentIndex(2);
        m_suspend = false;
        return;
    }
    if (it.kind == ItemKind::Image) {
        m_video->clear();
        m_image->setImage(it.common().sourcePath);
        m_stack->setCurrentIndex(0);
    } else {
        m_image->clear();
        m_video->setItem(it.common().id);
        m_stack->setCurrentIndex(1);
    }
    m_suspend = false;
}

} // namespace vlip
