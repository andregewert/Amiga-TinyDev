{ {$NAME}.pas - {$PURPOSE}.

  Copyright (c) {$YEAR} {$AUTHOR}
  {$AUTHORMAIL}
  Created {$DATE} }
program {$NAME};

{$MODE OBJFPC}{$H+}

uses
  {$NAME}_util;

begin
  if ParamCount > 0 then
    WriteLn(Greeting(ParamStr(1)))
  else
    WriteLn(Greeting('World'));
end.
