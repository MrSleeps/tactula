#pragma once

// Move the original settings namespace into the unified TouchDeck namespace
// without losing credentials if NVS is short of space or power is interrupted.
// Returns true when the new namespace can be used; false means keep using the
// legacy namespace and retry on the next boot.
bool migrateSettingsNamespace();
