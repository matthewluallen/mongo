/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#pragma once

#include "mongo/db/catalog/import_options.h"
#include "mongo/db/catalog/validate/validate_results.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_error_util.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_recovery_unit.h"
#include "mongo/util/string_map.h"

namespace mongo {

inline constexpr auto kWiredTigerEngineName = "wiredTiger"_sd;

class BSONObjBuilder;
class OperationContext;
class WiredTigerConfigParser;

class WiredTigerKVEngine;
class WiredTigerSessionCache;

struct WiredTigerItem : public WT_ITEM {
    WiredTigerItem(const void* d, size_t s) {
        data = d;
        size = s;
    }
    WiredTigerItem(const std::string& str) {
        data = str.c_str();
        size = str.size();
    }
    // NOTE: do not call Get() on a temporary.
    // The pointer returned by Get() must not be allowed to live longer than *this.
    WT_ITEM* Get() {
        return this;
    }
    const WT_ITEM* Get() const {
        return this;
    }
};

class WiredTigerUtil {
    WiredTigerUtil(const WiredTigerUtil&) = delete;
    WiredTigerUtil& operator=(const WiredTigerUtil&) = delete;

private:
    WiredTigerUtil();

public:
    static constexpr StringData kConfigStringField = "configString"_sd;

    /**
     * Fetch the type and source fields out of the colgroup metadata.  'tableUri' must be a
     * valid table: uri.
     */
    static void fetchTypeAndSourceURI(WiredTigerRecoveryUnit&,
                                      const std::string& tableUri,
                                      std::string* type,
                                      std::string* source);

    static bool collectConnectionStatistics(WiredTigerKVEngine* engine, BSONObjBuilder& bob);

    /**
     * Reads the WT database statistics table using the URI and exports all keys to BSON as string
     * elements. Additionally, adds the 'uri' field to output document.
     *
     * A filter can be specified to skip desired fields.
     */
    static Status exportTableToBSON(WT_SESSION* s,
                                    const std::string& uri,
                                    const std::string& config,
                                    BSONObjBuilder& bob);
    static Status exportTableToBSON(WT_SESSION* s,
                                    const std::string& uri,
                                    const std::string& config,
                                    BSONObjBuilder& bob,
                                    const std::vector<std::string>& filter);

    /**
     * Creates an import configuration string suitable for the 'config' parameter in
     * WT_SESSION::create() given the storage engines metadata retrieved during the export.
     *
     * Returns the FailedToParse status if the storage engine metadata object is malformed.
     */
    static StatusWith<std::string> generateImportString(StringData ident,
                                                        const BSONObj& storageMetadata,
                                                        const ImportOptions& importOptions);

    /**
     * Creates the configuration string for the 'backup_restore_target' config option passed into
     * wiredtiger_open().
     *
     * When restoring from a backup, WiredTiger will only restore the table objects present in the
     * dbpath. WiredTiger will remove all the metadata entries for the tables that are not listed in
     * the list from the reconstructed metadata.
     */
    static std::string generateRestoreConfig();

    /**
     * Returns true if WiredTiger startup will restore from a backup.
     */
    static bool willRestoreFromBackup();

    /**
     * Appends information about the storage engine's currently available snapshots and the settings
     * that affect that window of maintained history.
     *
     * "snapshot-window-settings" : {
     *      "total number of SnapshotTooOld errors" : <num>,
     *      "minimum target snapshot window size in seconds" : <num>,
     *      "current available snapshot window size in seconds" : <num>,
     *      "latest majority snapshot timestamp available" : <num>,
     *      "oldest majority snapshot timestamp available" : <num>
     * }
     */
    static void appendSnapshotWindowSettings(WiredTigerKVEngine* engine, BSONObjBuilder* bob);

    /**
     * Gets the creation metadata string for a collection or index at a given URI. Accepts an
     * OperationContext or session.
     *
     * This returns more information, but is slower than getMetadata().
     */
    static StatusWith<std::string> getMetadataCreate(WiredTigerRecoveryUnit&, StringData uri);
    static StatusWith<std::string> getMetadataCreate(WT_SESSION* session, StringData uri);

    /**
     * Gets the entire metadata string for collection or index at URI. Accepts an OperationContext
     * or session.
     */
    static StatusWith<std::string> getMetadata(WiredTigerRecoveryUnit&, StringData uri);
    static StatusWith<std::string> getMetadata(WT_SESSION* session, StringData uri);

    /**
     * Reads app_metadata for collection/index at URI as a BSON document.
     */
    static Status getApplicationMetadata(WiredTigerRecoveryUnit&,
                                         StringData uri,
                                         BSONObjBuilder* bob);

    static StatusWith<BSONObj> getApplicationMetadata(WiredTigerRecoveryUnit&, StringData uri);

    /**
     * Validates formatVersion in application metadata for 'uri'.
     * Version must be numeric and be in the range [minimumVersion, maximumVersion].
     * URI is used in error messages only. Returns actual version.
     */
    static StatusWith<int64_t> checkApplicationMetadataFormatVersion(WiredTigerRecoveryUnit&,
                                                                     StringData uri,
                                                                     int64_t minimumVersion,
                                                                     int64_t maximumVersion);

    /**
     * Validates the 'configString' specified as a collection or index creation option.
     */
    static Status checkTableCreationOptions(const BSONElement& configElem);

    /**
     * Reads individual statistics using URI.
     * List of statistics keys WT_STAT_* can be found in wiredtiger.h.
     */
    static StatusWith<int64_t> getStatisticsValue(WT_SESSION* session,
                                                  const std::string& uri,
                                                  const std::string& config,
                                                  int statisticsKey);

    static int64_t getEphemeralIdentSize(WT_SESSION* s, const std::string& uri);

    static int64_t getIdentSize(WT_SESSION* s, const std::string& uri);

    /**
     * Returns the bytes available for reuse for an ident. This is the amount of allocated space on
     * disk that is not storing any data.
     */
    static int64_t getIdentReuseSize(WT_SESSION* s, const std::string& uri);

    /**
     * Returns the bytes compaction may reclaim for an ident. This is the amount of allocated space
     * on disk that can be potentially reclaimed.
     */
    static int64_t getIdentCompactRewrittenExpectedSize(WT_SESSION* s, const std::string& uri);

    /**
     * Return amount of memory to use for the WiredTiger cache based on either the startup
     * option chosen or the amount of available memory on the host.
     */
    static size_t getCacheSizeMB(double requestedCacheSizeGB);

    class ErrorAccumulator : public WT_EVENT_HANDLER {
    public:
        explicit ErrorAccumulator(StringSet* errors);

    private:
        static int onError(WT_EVENT_HANDLER* handler,
                           WT_SESSION* session,
                           int error,
                           const char* message);

        using ErrorHandler = int (*)(WT_EVENT_HANDLER*, WT_SESSION*, int, const char*);

        StringSet* const _errors;
        const ErrorHandler _defaultErrorHandler;
    };

    /**
     * Calls WT_SESSION::validate() on a side-session to ensure that your current transaction
     * isn't left in an invalid state.
     *
     * If errors is non-NULL, all error messages will be appended to the array.
     */
    static int verifyTable(WiredTigerRecoveryUnit&,
                           const std::string& uri,
                           StringSet* errors = nullptr);

    /**
     * Checks the table logging setting in the metadata for the given uri, comparing it against
     * 'isLogged'. Populates 'valid', 'errors', and 'warnings' accordingly.
     */
    static void validateTableLogging(WiredTigerRecoveryUnit&,
                                     StringData uri,
                                     bool isLogged,
                                     boost::optional<StringData> indexName,
                                     ValidateResultsIf& validationResult);

    static bool useTableLogging(const NamespaceString& nss);

    static Status setTableLogging(WiredTigerRecoveryUnit&, const std::string& uri, bool on);

    /**
     * Generates a WiredTiger connection configuration given the LOGV2 WiredTiger components
     * verbosity levels.
     */
    static std::string generateWTVerboseConfiguration();

    /**
     * Casts unsigned 64-bit statistics value to T.
     * If original value exceeds maximum value of T, return max(T).
     */
    template <typename T>
    static T castStatisticsValue(uint64_t statisticsValue);

    /**
     * Gets the WiredTiger configuration string from storage engine collection options.
     */
    static boost::optional<std::string> getConfigStringFromStorageOptions(const BSONObj& options);

    /**
     * Sets the WiredTiger configuration string to storage engine collection options.
     */
    static BSONObj setConfigStringToStorageOptions(const BSONObj& options,
                                                   const std::string& configString);

    /**
     * Removes encryption configuration from a config string. Should only be applied on custom
     * config strings on secondaries. Fixes an issue where encryption configuration might be
     * replicated to non-encrypted nodes, or nodes with different encryption options, causing
     * initial sync or replication to fail. See SERVER-68122.
     */
    static void removeEncryptionFromConfigString(std::string* configString);

    /**
     * Removes encryption configuration from storage engine collection options.
     * See CollectionOptions.storageEngine and WiredTigerUtil::removeEncryptionFromConfigString().
     * TODO(SERVER-81069): Remove this since it's intrinsically tied to encryption options only.
     */
    static BSONObj getSanitizedStorageOptionsForSecondaryReplication(const BSONObj& options);

    /**
     * Background compaction should not be executed if:
     * - the feature flag is disabled or,
     * - it is an in-memory configuration,
     * - checkpoints are disabled or,
     * - user writes are not allowed.
     */
    static Status canRunAutoCompact(bool isEphemeral);

private:
    /**
     * Casts unsigned 64-bit statistics value to T.
     * If original value exceeds 'maximumResultType', return 'maximumResultType'.
     */
    template <typename T>
    static T _castStatisticsValue(uint64_t statisticsValue, T maximumResultType);
};

class WiredTigerConfigParser {
    WiredTigerConfigParser(const WiredTigerConfigParser&) = delete;
    WiredTigerConfigParser& operator=(const WiredTigerConfigParser&) = delete;

public:
    WiredTigerConfigParser(StringData config) {
        invariantWTOK(
            wiredtiger_config_parser_open(nullptr, config.rawData(), config.size(), &_parser),
            nullptr);
    }

    WiredTigerConfigParser(const WT_CONFIG_ITEM& nested) {
        invariant(nested.type == WT_CONFIG_ITEM::WT_CONFIG_ITEM_STRUCT);
        invariantWTOK(wiredtiger_config_parser_open(nullptr, nested.str, nested.len, &_parser),
                      nullptr);
    }

    ~WiredTigerConfigParser() {
        invariantWTOK(_parser->close(_parser), nullptr);
    }

    int next(WT_CONFIG_ITEM* key, WT_CONFIG_ITEM* value) {
        return _parser->next(_parser, key, value);
    }

    int get(const char* key, WT_CONFIG_ITEM* value) {
        return _parser->get(_parser, key, value);
    }

private:
    WT_CONFIG_PARSER* _parser;
};

// static
template <typename ResultType>
ResultType WiredTigerUtil::castStatisticsValue(uint64_t statisticsValue) {
    return _castStatisticsValue<ResultType>(statisticsValue,
                                            std::numeric_limits<ResultType>::max());
}

// static
template <typename ResultType>
ResultType WiredTigerUtil::_castStatisticsValue(uint64_t statisticsValue,
                                                ResultType maximumResultType) {
    return statisticsValue > static_cast<uint64_t>(maximumResultType)
        ? maximumResultType
        : static_cast<ResultType>(statisticsValue);
}

}  // namespace mongo
