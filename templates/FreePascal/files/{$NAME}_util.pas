{ {$NAME}_util.pas - The routines of {$NAME}.

  Copyright (c) {$YEAR} {$AUTHOR}
  {$AUTHORMAIL}
  Created {$DATE} }
unit {$NAME}_util;

{$MODE OBJFPC}{$H+}

interface

{ Returns the greeting for a name. }
function Greeting(const Who: string): string;

implementation

function Greeting(const Who: string): string;
begin
  Result := 'Hello, ' + Who + '!';
end;

end.
