#pragma once

#include <QMainWindow>
#include <QListView>
#include <QStackedWidget>
#include <QSplitter>
#include <QLabel>
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
    void onLoadHistory();

    // function bar actions
    void onAddFriend();
    void onDeleteFriend();
    void onBlockFriend();
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

private:
    void setupUi();
    void setupConnections();
    void updateRightPanel();

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
    QStackedWidget *right_stack_;
    InfoPanel *info_panel_;
    FunctionBar *function_bar_;
};
