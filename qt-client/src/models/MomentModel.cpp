#include "MomentModel.h"

MomentModel::MomentModel(QObject *parent)
    : QAbstractListModel(parent) {}

int MomentModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return static_cast<int>(moments_.size());
}

QVariant MomentModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= moments_.size())
        return {};

    const auto &m = moments_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case ContentRole:
        return m.content;
    case SenderIdRole:
        return QVariant::fromValue(m.sender_id);
    case SenderNameRole:
        return m.sender_name;
    case TimestampRole:
        return QVariant::fromValue(m.timestamp);
    default:
        return {};
    }
}

void MomentModel::setMoments(const QVector<MomentItem> &moments) {
    beginResetModel();
    moments_ = moments;
    endResetModel();
}

void MomentModel::appendMoment(const MomentItem &moment) {
    beginInsertRows(QModelIndex(), moments_.size(), moments_.size());
    moments_.append(moment);
    endInsertRows();
}
