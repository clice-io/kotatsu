// Where kota::ipc::lsp answers other than the spec, or than VS Code, says:
// protocol.h reading and writing the metaModel's types (protocol_values.ts),
// and LineMap converting positions (position.test.ts). The tests consult this
// list: what a deviation would change is not generated, and what it lets
// through is taken as read.
//
// An entry named after a finding (P4.x in the ipc plan) is a bug: the change
// that fixes it deletes its entry, and the tests then check the fix. An entry
// marked "design" is how kotatsu reads LSP on purpose.

export type Deviation =
  | "shadowedAlternatives"
  | "falseOptionalBoolean"
  | "nullOptional"
  | "openEnumerations"
  | "unknownProperties"
  | "fullUinteger"
  | "linePastEnd"
  | "loneCarriageReturn"
  | "insideSurrogatePair";

export const deviations: Partial<Record<Deviation, string>> = {
  shadowedAlternatives:
    "P4.1, the rest: an untagged variant reads a value as its first " +
    "alternative that reads it, and drops what that one lacks. Derived " +
    "structures come first now, but unrelated ones still read each other's " +
    "values: TextDocumentFilter before NotebookCellTextDocumentFilter in " +
    "DocumentFilter, SymbolInformation before WorkspaceSymbol in the result " +
    "of workspace/symbol.",
  falseOptionalBoolean:
    "design (D2.2): an optional boolean reads absent as false, so it writes " +
    "false as absent; but for the TRI_STATE_BOOLEANS (metamodel.ts).",
  nullOptional:
    "design: an optional property reads null as absent, but for a boolean " +
    "that reads absent as false.",
  openEnumerations:
    "design: an enumeration reads values it does not know, of its base type, " +
    "so that a newer peer's values do not fail the whole message.",
  unknownProperties: "design: a structure ignores properties it does not know.",
  fullUinteger:
    "design: uinteger is std::uint32_t, which reads up to 2^32 - 1 where the " +
    "spec stops at 2^31 - 1.",
  linePastEnd:
    "design: LineMap has no offset for a line past the last one, where VS " +
    "Code answers the end of the text.",
  loneCarriageReturn:
    "design (D6): LineMap ends lines at \\n only, so that the line starts " +
    "callers persist stay valid; LSP ends them at a lone \\r too.",
  insideSurrogatePair:
    "design: LineMap has no offset for a character between the two halves of " +
    "a surrogate pair, which is no place in the text; VS Code answers the " +
    "offset between them.",
};
