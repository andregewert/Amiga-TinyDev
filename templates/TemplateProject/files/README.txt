{$PROJECT}

template.json describes the template, files/ holds what it copies.

  Build/Build Project (Amiga+E)  checks template.json with Sprout
  Build/Run (Amiga+R)            makes a project from it in RAM:TemplateTest

The Run action "Install" copies this drawer to Templates/{$ID} of TinyDev;
the wizard lists it from then on.
