# The station runs Windows PowerShell 5.1: lint for its syntax as well as PowerShell 7's.
@{
    ExcludeRules = @('PSAvoidUsingWriteHost', 'PSUseShouldProcessForStateChangingFunctions', 'PSProvideCommentHelp')
    Rules = @{
        PSUseCompatibleSyntax = @{ Enable = $true; TargetVersions = @('5.1', '7.0') }
    }
}
