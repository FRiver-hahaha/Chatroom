#pragma once

#include <QWidget>
#include <QListView>
#include <QTextEdit>
#include <QPushButton>
#include "models/MomentModel.h"

class MomentWindow : public QWidget {
    Q_OBJECT
public:
    explicit MomentWindow(QWidget *parent = nullptr);

    void refresh();

private slots:
    void onRefresh();
    void onPublish();

private:
    QListView *moment_list_;
    MomentModel *moment_model_;
    QTextEdit *input_edit_;
    QPushButton *publish_btn_;
};
