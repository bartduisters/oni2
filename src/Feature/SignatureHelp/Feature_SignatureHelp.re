/* Feature_SignatureHelp.re
   This feature project contains logic related to Signature Help
    */
open Oni_Core;
open Revery;
open Revery.UI;
open Revery.UI.Components;
open EditorCoreTypes;

module Log = (val Log.withNamespace("Oni.Feature.SignatureHelp"));
module IDGenerator =
  Oni_Core.Utility.IDGenerator.Make({});

[@deriving show({with_path: false})]
type provider = {
  handle: int,
  selector: list(Exthost.DocumentFilter.t),
  metadata: Exthost.SignatureHelp.ProviderMetadata.t,
};

type model = {
  shown: bool,
  providers: list(provider),
  triggeredFrom: option([ | `CommandPalette]),
  lastRequestID: option(int),
  signatures: list(Exthost.SignatureHelp.Signature.t),
  activeSignature: option(int),
  activeParameter: option(int),
};

let initial = {
  shown: false,
  providers: [],
  triggeredFrom: None,
  lastRequestID: None,
  signatures: [],
  activeSignature: None,
  activeParameter: None,
};

[@deriving show({with_path: false})]
type command =
  | Show;

[@deriving show({with_path: false})]
type msg =
  | Command(command)
  | ProviderRegistered(provider)
  | InfoReceived({
      signatures: list(Exthost.SignatureHelp.Signature.t),
      activeSignature: int,
      activeParameter: int,
      requestID: int,
    })
  | RequestFailed(string);

type outmsg =
  | Nothing
  | Effect(Isolinear.Effect.t(msg))
  | Error(string);

module Commands = {
  open Feature_Commands.Schema;

  let show =
    define(
      ~category="Signature Help",
      ~title="Show signature help",
      "editor.action.showSignatureHelp", // This doesn't exist in VSCode, but is similar to the hover command.
      Command(Show),
    );
};

module Contributions = {
  let commands = Commands.[show];
};

let getEffectsForLocation =
    (~buffer, ~location, ~extHostClient, ~model, ~context, ~requestID) => {
  let filetype =
    buffer
    |> Oni_Core.Buffer.getFileType
    |> Option.value(~default="plaintext");

  let matchingProviders =
    model.providers
    |> List.filter(({selector, _}) =>
         Exthost.DocumentSelector.matches(~filetype, selector)
       );

  matchingProviders
  |> List.map(provider =>
       Service_Exthost.Effects.LanguageFeatures.provideSignatureHelp(
         ~handle=provider.handle,
         ~uri=Oni_Core.Buffer.getUri(buffer),
         ~position=location,
         ~context,
         extHostClient,
         res =>
         switch (res) {
         | Ok({signatures, activeSignature, activeParameter, _}) =>
           InfoReceived({
             signatures,
             activeSignature,
             activeParameter,
             requestID,
           })
         | Error(s) => RequestFailed(s)
         }
       )
     )
  |> Isolinear.Effect.batch;
};

let update = (~maybeBuffer, ~maybeEditor, ~extHostClient, model, msg) =>
  switch (msg) {
  | Command(Show) =>
    switch (maybeBuffer, maybeEditor) {
    | (Some(buffer), Some(editor)) =>
      let requestID = IDGenerator.get();
      let context =
        Exthost.SignatureHelp.RequestContext.{
          triggerKind: Exthost.SignatureHelp.TriggerKind.Invoke,
          triggerCharacter: None,
          isRetrigger: false // TODO: actually determine if it's a retrigger
        };

      let effects =
        getEffectsForLocation(
          ~buffer,
          ~location=Feature_Editor.Editor.getPrimaryCursor(~buffer, editor),
          ~extHostClient,
          ~model,
          ~context,
          ~requestID,
        );

      (
        {
          ...model,
          shown: true,
          triggeredFrom: Some(`CommandPalette),
          lastRequestID: Some(requestID),
        },
        Effect(effects),
      );
    | _ => (model, Nothing)
    }
  | ProviderRegistered(provider) => (
      {...model, providers: [provider, ...model.providers]},
      Nothing,
    )
  | InfoReceived({signatures, activeSignature, activeParameter, requestID}) =>
    switch (model.lastRequestID) {
    | Some(reqID) when reqID == requestID => (
        {
          ...model,
          signatures,
          activeSignature: Some(activeSignature),
          activeParameter: Some(activeParameter),
        },
        Nothing,
      )
    | _ => (model, Nothing)
    }
  | RequestFailed(str) =>
    Log.warnf(m => m("Request failed : %s", str));
    (model, Error(str));
  };
