/**
 *    Copyright (C) 2021-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include <mutex>

#include <boost/move/utility_core.hpp>
#include <boost/optional/optional.hpp>

#include "mongo/db/operation_context.h"
#include "mongo/db/storage/damage_vector.h"
#include "mongo/db/storage/record_store.h"
#include "mongo/db/transaction_resources.h"

namespace mongo {
namespace {
void validateWriteAllowed(RecoveryUnit& ru) {
    uassert(ErrorCodes::IllegalOperation,
            "Cannot execute a write operation in read-only mode",
            !ru.readOnly());
}

}  // namespace

RecordStore::RecordStore(boost::optional<UUID> uuid, StringData identName, bool isCapped)
    : _ident(std::make_shared<Ident>(identName.toString())),
      _uuid(uuid),
      _cappedInsertNotifier(isCapped ? std::make_shared<CappedInsertNotifier>() : nullptr) {}

void RecordStore::deleteRecord(OperationContext* opCtx, const RecordId& dl) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    doDeleteRecord(opCtx, dl);
}

Status RecordStore::insertRecords(OperationContext* opCtx,
                                  std::vector<Record>* inOutRecords,
                                  const std::vector<Timestamp>& timestamps) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    return doInsertRecords(opCtx, inOutRecords, timestamps);
}

Status RecordStore::updateRecord(OperationContext* opCtx,
                                 const RecordId& recordId,
                                 const char* data,
                                 int len) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    return doUpdateRecord(opCtx, recordId, data, len);
}

StatusWith<RecordData> RecordStore::updateWithDamages(OperationContext* opCtx,
                                                      const RecordId& loc,
                                                      const RecordData& oldRec,
                                                      const char* damageSource,
                                                      const DamageVector& damages) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    return doUpdateWithDamages(opCtx, loc, oldRec, damageSource, damages);
}

Status RecordStore::truncate(OperationContext* opCtx) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    return doTruncate(opCtx);
}

Status RecordStore::rangeTruncate(OperationContext* opCtx,
                                  const RecordId& minRecordId,
                                  const RecordId& maxRecordId,
                                  int64_t hintDataSizeDiff,
                                  int64_t hintNumRecordsDiff) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    invariant(minRecordId != RecordId() || maxRecordId != RecordId(),
              "Ranged truncate must have one bound defined");
    invariant(minRecordId <= maxRecordId, "Start position cannot be after end position");
    return doRangeTruncate(opCtx, minRecordId, maxRecordId, hintDataSizeDiff, hintNumRecordsDiff);
}

void RecordStore::cappedTruncateAfter(OperationContext* opCtx,
                                      const RecordId& end,
                                      bool inclusive,
                                      const AboutToDeleteRecordCallback& aboutToDelete) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    doCappedTruncateAfter(opCtx, end, inclusive, aboutToDelete);
}

bool RecordStore::haveCappedWaiters() const {
    return _cappedInsertNotifier && _cappedInsertNotifier.use_count() > 1;
}

void RecordStore::notifyCappedWaitersIfNeeded() {
    if (haveCappedWaiters())
        _cappedInsertNotifier->notifyAll();
}

StatusWith<int64_t> RecordStore::compact(OperationContext* opCtx, const CompactOptions& options) {
    validateWriteAllowed(*shard_role_details::getRecoveryUnit(opCtx));
    return doCompact(opCtx, options);
}


void CappedInsertNotifier::notifyAll() const {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    ++_version;
    _notifier.notify_all();
}

uint64_t CappedInsertNotifier::getVersion() const {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _version;
}

void CappedInsertNotifier::waitUntil(OperationContext* opCtx,
                                     uint64_t prevVersion,
                                     Date_t deadline) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    opCtx->waitForConditionOrInterruptUntil(_notifier, lk, deadline, [this, prevVersion]() {
        return _dead || prevVersion != _version;
    });
}

void CappedInsertNotifier::kill() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _dead = true;
    _notifier.notify_all();
}

bool CappedInsertNotifier::isDead() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _dead;
}

}  // namespace mongo
