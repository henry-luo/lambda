// D11.2.4: embedded trusted entry, never an app-selected script.
import updater: lambda.security.tuf.update
import json: lambda.security.tuf.json;
{begin_refresh: updater.begin_refresh, advance: updater.advance, description: json.document}
