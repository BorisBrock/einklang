#include "ChatDatabase.h"

#include <cctype>
#include <print>
#include <string_view>

#include <sqlite3.h>

// ---------------------------------------------------------------------------
// Destructor / lifecycle
// ---------------------------------------------------------------------------

ChatDatabase::~ChatDatabase()
{
	Close();
}

bool ChatDatabase::Open(const std::string& path)
{
	if (sqlite3_open(path.c_str(), &mDb) != SQLITE_OK)
	{
		std::println("[DB] Failed to open '{}': {}", path, sqlite3_errmsg(mDb));
		sqlite3_close(mDb);
		mDb = nullptr;
		return false;
	}

	// Enable WAL mode for better concurrent write performance.
	sqlite3_exec(mDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

	std::println("[DB] Opened database '{}'.", path);
	return true;
}

void ChatDatabase::Close()
{
	if (mDb)
	{
		sqlite3_close(mDb);
		mDb = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string ChatDatabase::TableName(const std::string& roomName)
{
	std::string name = "room_";
	for (const char c : roomName)
		name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
	return name;
}

// ---------------------------------------------------------------------------
// Schema / DML
// ---------------------------------------------------------------------------

void ChatDatabase::EnsureRoomTable(const std::string& roomName)
{
	if (!mDb)
		return;

	// Create the table with the full schema (new databases).
	const std::string createSql =
		"CREATE TABLE IF NOT EXISTS \"" + TableName(roomName) + "\" ("
		"  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  user_name    TEXT    NOT NULL,"
		"  text         TEXT    NOT NULL DEFAULT '',"
		"  timestamp_ms INTEGER NOT NULL,"
		"  image_data   BLOB    DEFAULT NULL"
		");";

	char* errMsg = nullptr;
	if (sqlite3_exec(mDb, createSql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK)
	{
		std::println("[DB] Failed to create table for room '{}': {}", roomName, errMsg);
		sqlite3_free(errMsg);
		return;
	}

	// Migrate existing tables that were created without the image_data column.
	// Use PRAGMA table_info to check reliably rather than catching ALTER TABLE errors.
	bool hasImageData = false;
	const std::string pragmaSql =
		"PRAGMA table_info(\"" + TableName(roomName) + "\");";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(mDb, pragmaSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
	{
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
			if (colName && std::string_view{colName} == "image_data")
			{
				hasImageData = true;
				break;
			}
		}
		sqlite3_finalize(stmt);
	}

	if (!hasImageData)
	{
		const std::string alterSql =
			"ALTER TABLE \"" + TableName(roomName) + "\" ADD COLUMN image_data BLOB DEFAULT NULL;";
		if (sqlite3_exec(mDb, alterSql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK)
		{
			std::println("[DB] Failed to add image_data column to room '{}': {}", roomName, errMsg);
			sqlite3_free(errMsg);
		}
		else
		{
			std::println("[DB] Migrated room '{}': added image_data column.", roomName);
		}
	}
}

void ChatDatabase::InsertMessage(const std::string& roomName,
                                 const std::string& userName,
                                 const std::string& text,
                                 std::int64_t       timestampMs)
{
	if (!mDb)
		return;

	const std::string sql =
		"INSERT INTO \"" + TableName(roomName) + "\" (user_name, text, timestamp_ms) VALUES (?, ?, ?);";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(mDb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
	{
		std::println("[DB] Failed to prepare insert: {}", sqlite3_errmsg(mDb));
		return;
	}

	sqlite3_bind_text (stmt, 1, userName.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text (stmt, 2, text.c_str(),     -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 3, timestampMs);

	if (sqlite3_step(stmt) != SQLITE_DONE)
		std::println("[DB] Failed to insert message: {}", sqlite3_errmsg(mDb));

	sqlite3_finalize(stmt);
}

void ChatDatabase::InsertImageMessage(const std::string&              roomName,
                                      const std::string&              userName,
                                      const std::vector<std::uint8_t>& imageData,
                                      std::int64_t                    timestampMs)
{
	if (!mDb)
		return;

	const std::string sql =
		"INSERT INTO \"" + TableName(roomName) +
		"\" (user_name, text, timestamp_ms, image_data) VALUES (?, '', ?, ?);";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(mDb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
	{
		std::println("[DB] Failed to prepare image insert: {}", sqlite3_errmsg(mDb));
		return;
	}

	sqlite3_bind_text (stmt, 1, userName.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, timestampMs);
	sqlite3_bind_blob (stmt, 3, imageData.data(),
	                   static_cast<int>(imageData.size()), SQLITE_TRANSIENT);

	if (sqlite3_step(stmt) != SQLITE_DONE)
		std::println("[DB] Failed to insert image message: {}", sqlite3_errmsg(mDb));

	sqlite3_finalize(stmt);
}

std::vector<ChatMessage> ChatDatabase::LoadMessages(const std::string& roomName, std::size_t limit)
{
	std::vector<ChatMessage> messages;

	if (!mDb)
		return messages;

	std::string sql =
		"SELECT user_name, text, timestamp_ms, image_data"
		" FROM \"" + TableName(roomName) + "\""
		" ORDER BY id ASC";
	if (limit > 0)
		sql += " LIMIT -1 OFFSET MAX(0, (SELECT COUNT(*) FROM \"" + TableName(roomName) + "\") - " + std::to_string(limit) + ")";
	sql += ";";


	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(mDb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
	{
		// Table may not exist yet for newly added rooms — that is fine.
		return messages;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		ChatMessage msg;
		msg.userName  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
		msg.text      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
		msg.timestampMs = sqlite3_column_int64(stmt, 2);
		// Column 3: image_data BLOB (NULL for text messages)
		if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
		{
			const void* blob = sqlite3_column_blob(stmt, 3);
			const int   blobSize = sqlite3_column_bytes(stmt, 3);
			if (blob != nullptr && blobSize > 0)
			{
				const auto* blobBytes = static_cast<const std::uint8_t*>(blob);
				msg.imageData.assign(blobBytes, blobBytes + blobSize);
				msg.kind = ChatMessage::Kind::Image;
			}
		}
		messages.push_back(std::move(msg));
	}

	sqlite3_finalize(stmt);
	return messages;
}
