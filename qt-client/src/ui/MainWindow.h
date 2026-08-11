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
class ConfettiOverlay;
class BombOverlay;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void onLoginSuccess();

signals:
    void momentsClicked();

private slots:
    void onContactClicked(const QModelIndex &index);
    void onSendMessage(const QString &text);
    void onFileUpload();
    void onGameClicked();
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
    void onGameMove(const QString &move);

private:
    void setupUi();
    void setupConnections();
    void updateRightPanel();
    void showGameResult(const QString &playerMove);

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

    // overlays
    ConfettiOverlay *confetti_;
    BombOverlay *bomb_;
};
