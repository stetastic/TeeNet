CREATE TABLE IF NOT EXISTS devices (
  id_hash TEXT PRIMARY KEY,
  key_hash TEXT NOT NULL,
  first_seen INTEGER NOT NULL,
  last_seen INTEGER NOT NULL,
  version TEXT NOT NULL,
  build TEXT NOT NULL,
  hardware TEXT NOT NULL,
  consent_revision TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS admin_credentials (
  slot INTEGER PRIMARY KEY CHECK(slot=1), salt TEXT NOT NULL, password_hash TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS admin_sessions (
  token_hash TEXT PRIMARY KEY, expires INTEGER NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS admin_attempts (
  slot INTEGER PRIMARY KEY CHECK(slot=1), started INTEGER NOT NULL, attempts INTEGER NOT NULL
);
