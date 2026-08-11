#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

struct MomentItem {
    uint64_t sender_id = 0;
    QString sender_name;
    QString content;
    uint64_t timestamp = 0;
};

class MomentModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        SenderIdRole = Qt::UserRole + 1,
        SenderNameRole,
        ContentRole,
        TimestampRole
    };

    explicit MomentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setMoments(const QVector<MomentItem> &moments);
    void appendMoment(const MomentItem &moment);

private:
    QVector<MomentItem> moments_;
};
