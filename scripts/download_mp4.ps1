Push-Location "assets"
Invoke-WebRequest -OutFile "test-sample-0.mp4" -Uri "https://download.samplelib.com/mp4/sample-5s.mp4"
Invoke-WebRequest -OutFile "test-sample-1.mp4" -Uri "https://download.samplelib.com/mp4/sample-10s.mp4"
Invoke-WebRequest -OutFile "test-sample-2.mp4" -Uri "https://jsoncompare.org/LearningContainer/SampleFiles/Video/MP4/Sample-MP4-Video-File-Download.mp4"
Pop-Location