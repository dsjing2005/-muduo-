-- 聊天消息写入一次，按私聊双方或群号分页
CREATE TABLE IF NOT EXISTS ChatMessage (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    msgtype TINYINT UNSIGNED NOT NULL,
    sender_id INT NOT NULL,
    target_id INT NOT NULL,
    payload JSON NOT NULL,
    PRIMARY KEY (id),
    KEY chat_sender (msgtype,sender_id,target_id,id),
    KEY chat_recipient (msgtype,target_id,sender_id,id),
    KEY chat_group (msgtype,target_id,id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 离线补收只记录待发送的消息ID，正文仍从会话记录读取
CREATE TABLE IF NOT EXISTS ChatMessageOffline (
    userid INT NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (userid,message_id),
    KEY chat_offline_message (message_id),
    CONSTRAINT chat_offline_message_fk FOREIGN KEY (message_id) REFERENCES ChatMessage(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
