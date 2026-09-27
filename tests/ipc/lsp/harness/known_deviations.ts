// Where kota::ipc::lsp answers other than the spec, or than VS Code, says:
// protocol.h reading and writing the metaModel's types (protocol_values.ts),
// and LineMap converting positions (position.test.ts). The tests consult this
// list: what a deviation would change is not generated, and what it lets
// through is taken as read.
//
// An entry named after a finding (P4.x, N6 in the ipc plan) is a bug: the
// change that fixes it deletes its entry, and the tests then check the fix. An
// entry marked "design" is how kotatsu reads LSP on purpose; one marked with an
// open decision (D6) waits for it.

export type Deviation =
  | "shadowedAlternatives"
  | "absentRequiredNullable"
  | "triStateFalse"
  | "nullOptionalNullable"
  | "falseOptionalBoolean"
  | "nullOptional"
  | "openEnumerations"
  | "unknownProperties"
  | "fullUinteger"
  | "carriageReturnInLine"
  | "characterPastLineEnd"
  | "linePastEnd"
  | "loneCarriageReturn"
  | "insideSurrogatePair";

export const deviations: Partial<Record<Deviation, string>> = {
  shadowedAlternatives:
    "P4.1: an untagged variant reads a value as its first alternative that " +
    "reads it, and drops what that one lacks: TextEdit before " +
    "AnnotatedTextEdit, the 16 XOptions before XRegistrationOptions in " +
    "ServerCapabilities, SymbolInformation before WorkspaceSymbol.",
  absentRequiredNullable:
    "P4.1: a required property whose type admits null may be absent " +
    "(check_required_fields asks nothing of a std::optional).",
  triStateFalse:
    "P4.2: ExecutionSummary.success, WorkDoneProgressBegin.cancellable, " +
    "WorkDoneProgressReport.cancellable and " +
    "SemanticTokensClientCapabilities.augmentsSyntaxTokens drop false, which " +
    "is not the same as absent for them.",
  nullOptionalNullable:
    "P4.3: an optional property whose type admits null reads null as absent: " +
    "optional<nullable<T>> (workspaceFolders twice, activeParameter twice, " +
    "rootPath), and every optional LSPAny (data, initializationOptions, " +
    "experimental, ...).",
  falseOptionalBoolean:
    "design (D2.2): an optional boolean reads absent as false, so it writes " +
    "false as absent.",
  nullOptional:
    "design: an optional property reads null as absent, but for a boolean.",
  openEnumerations:
    "design: an enumeration reads values it does not know, of its base type, " +
    "so that a newer peer's values do not fail the whole message.",
  unknownProperties: "design: a structure ignores properties it does not know.",
  fullUinteger:
    "design: uinteger is std::uint32_t, which reads up to 2^32 - 1 where the " +
    "spec stops at 2^31 - 1.",
  carriageReturnInLine:
    "N6: LineMap counts the \\r of a \\r\\n as the line's last character, " +
    "so the offset of the \\n is one character further than VS Code says, " +
    "which puts it before the \\r.",
  characterPastLineEnd:
    "N6: LineMap has no offset for a character past the end of its line, " +
    "which LSP says stands for the line's end.",
  linePastEnd:
    "N6: LineMap has no offset for a line past the last one, where VS Code " +
    "answers the end of the text.",
  loneCarriageReturn:
    "D6: LineMap ends lines at \\n only; LSP ends them at a lone \\r too.",
  insideSurrogatePair:
    "design: LineMap has no offset for a character between the two halves of " +
    "a surrogate pair, which is no place in the text; VS Code answers the " +
    "offset between them.",
};

/** The optional booleans for which false and absent differ (P4.2, D2.2). */
export const TRI_STATE_BOOLEANS = new Set([
  "ExecutionSummary.success",
  "WorkDoneProgressBegin.cancellable",
  "WorkDoneProgressReport.cancellable",
  "SemanticTokensClientCapabilities.augmentsSyntaxTokens",
]);
