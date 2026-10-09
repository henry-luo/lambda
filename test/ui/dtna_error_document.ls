// An unhandled package error must reject the document without crashing teardown.
import ui: lambda.ui.dtna
ui.button({size:'invalid'}, "Invalid")^
