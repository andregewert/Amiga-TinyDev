{ Sample of Free Pascal for the tests of the parser. }
unit sample;

{$mode objfpc}
{$DEFINE FEATURE}

interface

uses
  SysUtils, Helper, Other in 'lib/other.pas';

const
  MaxItems = 16;
  Title: string = 'it''s';

type
  TColor = (red, green, blue);
  TRange = 0..15;
  TName = string[20];
  PPoint = ^TPoint;
  TPoint = record
    x, y: Integer;
    case Boolean of
      False: (tag: Byte);
      True: (flag: Char; note: Word);
  end;
  TBase = class;
  TBase = class(TObject)
  private
    FValue: Integer;
  public
    constructor Create(AValue: Integer);
    destructor Destroy; override;
    function Value: Integer; virtual;
    procedure SetValue(const AValue: Integer);
    property Current: Integer read FValue write SetValue;
  end;

var
  Counter, Other2: LongInt;

function Twice(a: Integer): Integer;
procedure Log(const Msg: string); forward;

implementation

{$I parts.inc}

label
  done;

var
  Hidden: Boolean;

constructor TBase.Create(AValue: Integer);
begin
  FValue := AValue;
end;

destructor TBase.Destroy;
begin
  inherited Destroy;
end;

function TBase.Value: Integer;
begin
  Result := FValue;
end;

procedure TBase.SetValue(const AValue: Integer);
begin
  FValue := AValue;
end;

function Twice(a: Integer): Integer;
var
  local: Integer;

  function Inner(b: Integer): Integer;
  begin
    Inner := b * 2;
  end;

begin
  local := Inner(a);
  if local > MaxItems then
    Twice := MaxItems
  else
    Twice := local;
end;

procedure Log(const Msg: string);
var
  i: Integer;
begin
  for i := 1 to 3 do
    WriteLn(Msg);
  while i > 0 do begin
    Dec(i);
  end;
  repeat
    Inc(i);
  until i = 3;
  case i of
    1: WriteLn('one');
    2: WriteLn('two');
  else
    WriteLn('other');
  end;
  try
    Hidden := True;
  except
    on E: Exception do
      WriteLn(E.Message);
  end;
  try
    Hidden := False;
  finally
    Counter := 0;
  end;
  with TBase.Create(1) do
    Free;
  asm
    move.l d0,d1 ; it's not Pascal: if (
  end;
end;

initialization
  Counter := Ord(red);
finalization
  Counter := 0;
end.

this text is ignored, even an unclosed ' string
