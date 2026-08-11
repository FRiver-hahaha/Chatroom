#include "MomentWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDateTime>
#include "state/ClientState.h"

MomentWindow::MomentWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("朋友圈");
    setMinimumSize(500, 600);

    auto *layout = new QVBoxLayout(this);

    auto *topBar = new QHBoxLayout();
    auto *titleLabel = new QLabel("朋友圈");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();

    auto *refreshBtn = new QPushButton("刷新");
    topBar->addWidget(refreshBtn);
    layout->addLayout(topBar);

    moment_model_ = new MomentModel(this);
    moment_list_ = new QListView();
    moment_list_->setModel(moment_model_);
    moment_list_->setStyleSheet(
        "QListView { background-color: #f8f8f8; border: none; }"
        "QListView::item { padding: 10px; border-bottom: 1px solid #e0e0e0; }");
    layout->addWidget(moment_list_, 1);

    auto *inputBar = new QWidget();
    auto *inputLayout = new QHBoxLayout(inputBar);
    input_edit_ = new QTextEdit();
    input_edit_->setPlaceholderText("分享你的想法...");
    input_edit_->setMaximumHeight(60);
    inputLayout->addWidget(input_edit_, 1);

    publish_btn_ = new QPushButton("发布");
    publish_btn_->setMinimumWidth(60);
    inputLayout->addWidget(publish_btn_);
    layout->addWidget(inputBar);

    connect(refreshBtn, &QPushButton::clicked, this, &MomentWindow::onRefresh);
    connect(publish_btn_, &QPushButton::clicked, this, &MomentWindow::onPublish);
}

void MomentWindow::refresh() {
    // TODO: server needs MOMENT_QUERY_REQ/RSP implementation
    QMessageBox::information(this, "提示", "动态功能需要服务端支持");
}

void MomentWindow::onRefresh() {
    refresh();
}

void MomentWindow::onPublish() {
    QString text = input_edit_->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // TODO: server needs MOMENT_PUBLISH_REQ/RSP implementation
    QMessageBox::information(this, "提示", "动态发送需要服务端支持");
    input_edit_->clear();
}
