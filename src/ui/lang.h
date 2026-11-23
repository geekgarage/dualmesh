// SPDX-License-Identifier: MIT
// DualMesh – brugerfladens sprog (indstillingen "language" under Skærm). Dansk er kildesproget;
// de andre sprog er indbygget (lang/*.txt -> src/ui/lang_builtin.h, tools/gen_lang.py) eller lagt
// på SD-kortet som /dualmesh/lang/<id>.txt (samme format; går forud for et indbygget af samme id).
#pragma once

namespace dm {
namespace lang {

void begin(bool sd_mounted);     // finder sprogene (indbyggede + SD)
int count();                     // 0 = Dansk
const char* nameAt(int i);       // visningsnavn ("Dansk", "English", ...)
// Vælger sproget ud fra navn eller id (tom/ukendt = dansk). Gælder for al tegning herefter.
void apply(const char* name_or_id);

}  // namespace lang
}  // namespace dm
