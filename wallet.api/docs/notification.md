# Notification Reference

## 地址余额变动

TODO

## 多重签名账户

### 多重签名账户状态变更

提示信息：@name 加入了多重签名账户 @account

#### iOS

```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_ACCOUNT_CHANGE",
        "loc-args": ["@name", "@account"]
    },
    "customContent": {
        "account_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_ACCOUNT_CHANGE",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_ACCOUNT_STATUS_CHANGE",
        "args": "@name|@account",
        "account_id": 3
    }
}
```

### 多重签名账户创建完成

提示信息：多重签名账户 @account 创建完成

#### iOS

```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_ACCOUNT_CREATED",
        "loc-args": ["@account"]
    },
    "customContent": {
        "account_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_ACCOUNT_CREATED",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_ACCOUNT_STATUS_CHANGE",
        "args": "@account",
        "account_id": 3
    }
}
```

### 多重签名账户取消

提示信息：@name 取消了多重签名账户 @account

#### iOS
```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_ACCOUNT_DELETE",
        "loc-args": ["@name", "@account"]
    },
    "customContent": {
        "account_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_ACCOUNT_DELETE",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_ACCOUNT_DELETE",
        "args": "@name|@account",
        "account_id": 3
    }
}
```

### 多重签名账户创建失败

提示信息：多重签名账户 @account 创建失败，请重试

#### iOS
```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_ACCOUNT_CREATE_FAILED",
        "loc-args": ["@account"]
    },
    "customContent": {
        "account_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_ACCOUNT_CREATE_FAILED",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_ACCOUNT_CREATE_FAILED",
        "args": "@account",
        "account_id": 3
    }
}
```

## 多重签名交易

### 新建多重签名交易

提示信息：@name 发起了多重签名交易，请审批

#### iOS
```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_TX_CREATE",
        "loc-args": ["@name"]
    },
    "customContent": {
        "account_id": 18,
        "tx_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_TX_CREATE",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_TX_CREATE",
        "args": "@name",
        "account_id": 18,
        "tx_id": 3
    }
}
```

### 多重签名交易状态变更

提示信息：
    
  * 多重签名交易状态变更：批准交易，交易成功

    @name 批准了多重签名交易，继续等待其他人审批

  * 多重签名交易状态变更：批准交易，交易待定

    @name 批准了多重签名交易，交易已经成功并发送

  * 多重签名交易状态变更：拒绝交易，交易失败

    @name 拒绝了多重签名交易，交易已被取消

  * 多重签名交易状态变更：拒绝交易，交易待定

    @name 拒绝了多重签名交易，继续等待其他人审批

#### iOS
```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_TX_CHANGE_APPROVED_APPROVED",  -- MULTISIG_TX_CHANGE_{participant_status}_{tx_status}
        //"loc-key": "MULTISIG_TX_CHANGE_APPROVED_TBD",
        //"loc-key": "MULTISIG_TX_CHANGE_DENIED_DENIED",
        //"loc-key": "MULTISIG_TX_CHANGE_DENIED_TBD",
        "loc-args": ["@name"]
    },
    "customContent": {
        "account_id": 18,
        "tx_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_TX_CHANGE",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_TX_CHANGE_APPROVED_APPROVED",
        "args": "@name",
        "account_id": 18,
        "tx_id": 3
    }
}
```

### 多重签名交易取消

提示信息：@name 取消了多重签名交易

#### iOS
```
{
    "expireTime": 259200,   // 3 days
    "alert": {
        "loc-key": "MULTISIG_TX_DELETE",
        "loc-args": ["@name"]
    },
    "customContent": {
        "account_id": 18,
        "tx_id": 3
    },
    "badge": 1,
    "sound": "default"
}
```

#### Android

```
{
    "content": "MULTISIG_TX_DELETE",
    "expireTime": 259200,       // 3 days
    "type": 2,
    "customContent": {
        "action": "MULTISIG_TX_DELETE",
        "args": "@name",
        "account_id": 18,
        "tx_id": 3
    }
}
```
