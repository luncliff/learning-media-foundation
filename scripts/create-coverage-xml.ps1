<#
.SYNOPSIS
    PowerShell script to create coverage for SonarCloud Analysis
.DESCRIPTION
    https://docs.microsoft.com/en-us/visualstudio/test/using-code-coverage-to-determine-how-much-code-is-being-tested
#>
using namespace System

# Codecoverage.exe (Azure Pipelines)
env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\Dynamic Code Coverage Tools;$env:Path"

# Acquire coverage file and generate temporary coverage file
$coverage_file = Get-ChildItem -Recurse "*.coverage";
if ($null -eq $coverage_file) {
    Write-Output "coverage file not found"
    return
}
Write-Output $coverage_file

$temp_coverage_xml_filepath = "./TestResults/coverage-report.xml"
CodeCoverage analyze /verbose /output:$temp_coverage_xml_filepath $coverage_file

Get-ChildItem "./TestResults"

# Filter lines with invalid line number 
#   and Create a new coverage xml
# $final_coverage_xml_filepath = "./TestResults/luncliff-media.coveragexml"
# $xml_lines = Get-Content $temp_coverage_xml_filepath
# foreach ($text in $xml_lines) {
#     if ($text -match 15732480) {
#         Write-Output "removed $text"
#         continue;
#     }
#     else {
#         Add-Content $final_coverage_xml_filepath $text;
#     }
# }
# Tree ./TestResults /F

# Display information of a new coverage xml
# Get-ChildItem $final_coverage_xml_filepath
# Get-Content   $final_coverage_xml_filepath
