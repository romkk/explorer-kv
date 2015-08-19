/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Log2Producer.h"


//////////////////////////////  MemTxRepository  ///////////////////////////////
bool MemTxRepository::addTx(const CTransaction &tx,
                            vector<uint256> &conflictTxs) {
  const uint256 txhash = tx.GetHash();

  // 返回第一层的冲突交易
  for (auto &it : tx.vin) {
    TxOutputKey out(it.prevout.hash, it.prevout.n);
    if (spentOutputs_.find(out) != spentOutputs_.end()) {
      conflictTxs.push_back(spentOutputs_[out]);
      LOG_WARN("conflict tx input: %s:%d, already spent in tx: %s",
               it.prevout.hash.ToString().c_str(), it.prevout.n,
               spentOutputs_[out].ToString().c_str());
    }
  }

  // 没有冲突，加入到内存
  if (conflictTxs.size() == 0) {
    assert(memTxs_.find(txhash) == memTxs_.end());

    // 标记内存中已经花费的output
    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      spentOutputs_.insert(make_pair(out, txhash));
    }
    memTxs_[txhash] = tx;

    return true;
  }

  // 有冲突，遍历冲突交易，迭代找出冲突交易的后续交易。深度优先的遍历方式。
  for (size_t i = 0; i < conflictTxs.size(); i++) {
    const uint256 chash = conflictTxs[i];

    // 该hash被谁花费了
    assert(memTxs_.find(chash) != memTxs_.end());
    for (int j = 0; j < memTxs_[chash].vout.size(); j++) {
      TxOutputKey out(chash, j);
      if (spentOutputs_.find(out) != spentOutputs_.end()) {
        conflictTxs.push_back(spentOutputs_[out]);
      }
    }
  }

  return false;
}

void MemTxRepository::removeTxs(const vector<uint256> &txhashs) {
  for (auto &hash : txhashs) {
    assert(memTxs_.find(hash) != memTxs_.end());
    CTransaction &tx = memTxs_[hash];

    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      auto it2 = spentOutputs_.find(out);
      assert(it2 != spentOutputs_.end());
      spentOutputs_.erase(it2);
    }
    memTxs_.erase(hash);
  }
}


///////////////////////////////  Log2Producer  /////////////////////////////////
void Log2Producer::removeUnreadyLog2() {
  string sql;
  //
  // 清理临时的记录（未完整状态的），临时记录都在 txlogs2 最后，且连续的。
  // sql: 总是清理最后的 N 条记录(如果里面有 `is_ready` = 0的话)
  //
  sql = "DELETE FROM `txlogs2` WHERE ";
  sql += " `id` >= ((SELECT * FROM (SELECT MAX(`id`) FROM `txlogs2`) AS `t1`) - 1000)";
  sql += " AND `is_ready` = 0 ";

  uint64 delNum = 0;
  while ((delNum = db_.update(sql)) > 0) {
    LOG_INFO("remove unready log2 records: %llu", delNum);
  };
}

static void _initLog2_loadUnconfirmedTxs(MySQLConnection &db,
                                         MemTxRepository &memRepo) {
  string sql = "SELECT `tx_hash` FROM `0_unconfirmed_txs` ORDER BY `position`";
  MySQLResult res;
  char **row = nullptr;
  db.query(sql, res);
  if (res.numRows() == 0)  {
    return;
  }

  while ((row = res.nextRow()) != nullptr) {
    const uint256 hash(row[0]);
    const string txHex = getTxHexByHash(db, hash);

    vector<uint256> conflictTxs;
    CTransaction tx;
    if (!DecodeHexTx(tx, txHex)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", txHex.c_str());
    }
    if (memRepo.addTx(tx, conflictTxs) == false) {
      THROW_EXCEPTION_DBEX("add tx to mem repo fail, tx: %s", hash.ToString().c_str());
    }
  }
}

void Log2Producer::initLog2() {
  LogScope ls("Log2Producer::initLog2()");
  string sql;
  MySQLResult res;
  char **row;

  //
  // 清理临时的记录（未完整状态的）
  //
  removeUnreadyLog2();

  //
  // 找最后一个块记录，即最后一条 block_id 非零的记录
  //
  int64_t blockId = 0;
  {
    sql = "SELECT `block_id` FROM `txlogs2` WHERE `block_id`>0 ORDER BY `id` DESC LIMIT 1";
    db_.query(sql, res);
    if (res.numRows()) {
      row = res.nextRow();
      blockId = atoi64(row[0]);
    }
  }
  if (blockId > 0) {
    sql = Strings::Format("SELECT `block_hash`,`block_height` FROM `0_raw_blocks` WHERE `id` = %lld ",
                          blockId);
    db_.query(sql, res);
    if (res.numRows() == 0) {
      THROW_EXCEPTION_DBEX("can't find block by id: %lld", blockId);
    }
    row = res.nextRow();
    log2BlockHash_   = uint256(row[0]);
    log2BlockHeight_ = atoi(row[1]);
  } else {
    // 数据库没有记录，则以配置文件的块信息作为起始块
    log2BlockHash_   = uint256(Config::GConfig.get("log2.begin.block.hash"));
    log2BlockHeight_ = (int32_t)Config::GConfig.getInt("log2.begin.block.height");
  }
  LOG_INFO("log2 latest block, height: %d, hash: %s",
           log2BlockHeight_, log2BlockHash_.ToString().c_str());

  //
  // 载入未确认交易
  //
  _initLog2_loadUnconfirmedTxs(db_, memRepo_);

}
