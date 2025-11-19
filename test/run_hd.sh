#!/bin/bash

srt2dvbsub \
	--input ../../dvbzoo/build/Alita_Battle_Angel.bluray.1080p_25fps_ac3_eng_aac_eng_ac3_ger_aac_ger.ts \
	--output ../test/Alita_Battle_Angel.bluray.1080p_25fps_ac3_eng_aac_eng_ac3_ger_aac_ger_eng_dvbsub_eng.ts \
	--srt ../test/Alita.Battle.Angel.2019.en.25.srt,../test/Alita.Battle.Angel.2019.de.25.srt \
	--languages eng,deu \
	--ssaa 8 

