// Tune6 C3 fixture: cross every former fixed metadata limit so MIR tracking
// covers exact count/fill storage and the bulk instance-field metadata call.
class Tune6ExactCollection {
  static s0 = 0; static s1 = 1; static s2 = 2; static s3 = 3;
  static s4 = 4; static s5 = 5; static s6 = 6; static s7 = 7;
  static s8 = 8; static s9 = 9; static s10 = 10; static s11 = 11;
  static s12 = 12; static s13 = 13; static s14 = 14; static s15 = 15;
  static s16 = 16;

  f0 = 0; f1 = 1; f2 = 2; f3 = 3; f4 = 4; f5 = 5; f6 = 6; f7 = 7;
  f8 = 8; f9 = 9; f10 = 10; f11 = 11; f12 = 12; f13 = 13; f14 = 14;
  f15 = 15; f16 = 16; f17 = 17; f18 = 18; f19 = 19; f20 = 20;
  f21 = 21; f22 = 22; f23 = 23; f24 = 24; f25 = 25; f26 = 26;
  f27 = 27; f28 = 28; f29 = 29; f30 = 30; f31 = 31; f32 = 32;

  static { this.b0 = 0; } static { this.b1 = 1; }
  static { this.b2 = 2; } static { this.b3 = 3; }
  static { this.b4 = 4; } static { this.b5 = 5; }
  static { this.b6 = 6; } static { this.b7 = 7; }
  static { this.b8 = 8; }

  m0() { return 0; } m1() { return 1; } m2() { return 2; }
  m3() { return 3; } m4() { return 4; } m5() { return 5; }
  m6() { return 6; } m7() { return 7; } m8() { return 8; }
  m9() { return 9; } m10() { return 10; } m11() { return 11; }
  m12() { return 12; } m13() { return 13; } m14() { return 14; }
  m15() { return 15; } m16() { return 16; } m17() { return 17; }
  m18() { return 18; } m19() { return 19; } m20() { return 20; }
  m21() { return 21; } m22() { return 22; } m23() { return 23; }
  m24() { return 24; } m25() { return 25; } m26() { return 26; }
  m27() { return 27; } m28() { return 28; } m29() { return 29; }
  m30() { return 30; } m31() { return 31; } m32() { return 32; }
  m33() { return 33; } m34() { return 34; } m35() { return 35; }
  m36() { return 36; } m37() { return 37; } m38() { return 38; }
  m39() { return 39; } m40() { return 40; } m41() { return 41; }
  m42() { return 42; } m43() { return 43; } m44() { return 44; }
  m45() { return 45; } m46() { return 46; } m47() { return 47; }
  m48() { return 48; } m49() { return 49; } m50() { return 50; }
  m51() { return 51; } m52() { return 52; } m53() { return 53; }
  m54() { return 54; } m55() { return 55; } m56() { return 56; }
  m57() { return 57; } m58() { return 58; } m59() { return 59; }
  m60() { return 60; } m61() { return 61; } m62() { return 62; }
  m63() { return 63; } m64() { return 64; } m65() { return 65; }
  m66() { return 66; } m67() { return 67; } m68() { return 68; }
  m69() { return 69; } m70() { return 70; } m71() { return 71; }
  m72() { return 72; } m73() { return 73; } m74() { return 74; }
  m75() { return 75; } m76() { return 76; } m77() { return 77; }
  m78() { return 78; } m79() { return 79; } m80() { return 80; }
  m81() { return 81; } m82() { return 82; } m83() { return 83; }
  m84() { return 84; } m85() { return 85; } m86() { return 86; }
  m87() { return 87; } m88() { return 88; } m89() { return 89; }
  m90() { return 90; } m91() { return 91; } m92() { return 92; }
  m93() { return 93; } m94() { return 94; } m95() { return 95; }
  m96() { return 96; } m97() { return 97; } m98() { return 98; }
  m99() { return 99; } m100() { return 100; } m101() { return 101; }
  m102() { return 102; } m103() { return 103; } m104() { return 104; }
  m105() { return 105; } m106() { return 106; } m107() { return 107; }
  m108() { return 108; } m109() { return 109; } m110() { return 110; }
  m111() { return 111; } m112() { return 112; } m113() { return 113; }
  m114() { return 114; } m115() { return 115; } m116() { return 116; }
  m117() { return 117; } m118() { return 118; } m119() { return 119; }
  m120() { return 120; } m121() { return 121; } m122() { return 122; }
  m123() { return 123; } m124() { return 124; } m125() { return 125; }
  m126() { return 126; } m127() { return 127; } m128() { return 128; }
}

const tuned = new Tune6ExactCollection();
console.log(tuned.m128(), tuned.f32, Tune6ExactCollection.s16,
            Tune6ExactCollection.b8);
