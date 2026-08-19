#pragma once

#include <QMainWindow>
#include <QListView>
#include <QStackedWidget>
#include <QSplitter>
#include <QLabel>
#include <QModelIndex>
#include "state/ClientState.h"
#include "models/ChatMessageModel.h"
#include "models/ContactListModel.h"

class ChatInputBar;
class InfoPanel;
class FunctionBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void onLoginSuccess();

private slots:
    void onContactClicked(const QModelIndex &index);
    void onSendMessage(const QString &text);
    void onFileUpload();
    void onHistoryLoadRequested();
    void onFileTransferNotify(uint64_t transferId, uint64_t senderId,
                              const QString &senderName, const QString &fileName,
                              uint64_t fileSize);

    // function bar actions
    void onAddFriend();
    void onDeleteFriend();
    void onBlockFriend();
    void onUnblockFriend();
    void onQueryBlocked();
    void onFriendRequests();
    void onCreateGroup();
    void onJoinGroup();
    void onGroupInvites();
    void onEditProfile();
    void onDeleteAccount();

    void onQuitGroup();
    void onViewMembers();
    void onDismissGroup();
    void onApproveJoin();
    void onRemoveMember();
    void onAddAdmin();
    void onRemoveAdmin();

    void onContactsUpdated();
    void onMessagesUpdated();
    void onChatChanged();
    void onSystemNotification(const QString &text);
    void onIncomingMessage(uint64_t fromId, const QString &senderName, const QString &content);
    void onGroupMembersReceived(uint64_t groupId, const QStringList &members);
    void onBlockedUsersReceived(const QVector<ContactItem> &users);

private:
    void setupUi();
    void setupConnections();
    void updateRightPanel();
    void saveScrollState();
    void restoreScrollState();

    QSplitter *main_splitter_;

    // left
    QListView *contact_list_;
    ContactListModel *contact_model_;

    // center
    QLabel *chat_title_;
    QListView *message_list_;
    ChatMessageModel *message_model_;
    ChatInputBar *input_bar_;

    // right
    QWidget *right_panel_;
    InfoPanel *info_panel_;
    FunctionBar *function_bar_;

    // 历史记录加载（微信式上滑加载）
    bool history_loading_ = false;
    int history_limit_ = 50;
    QModelIndex top_visible_index_;
    bool at_bottom_ = true;

    QLabel *my_id_label_ = nullptr;
};