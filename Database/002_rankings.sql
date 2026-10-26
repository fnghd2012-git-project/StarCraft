-- 진행 중 순위는 Redis, 종료 결과는 이 테이블에 보관한다.
IF OBJECT_ID(N'dbo.PortfolioMatch', N'U') IS NULL
CREATE TABLE dbo.PortfolioMatch (
    Realm nvarchar(64) NOT NULL,
    MatchId char(32) NOT NULL,
    Winner int NOT NULL,
    Complete bit NOT NULL,
    EndReason varchar(32) NOT NULL,
    SavedAt datetime2 NOT NULL CONSTRAINT DF_PortfolioMatch_SavedAt DEFAULT SYSUTCDATETIME(),
    CONSTRAINT PK_PortfolioMatch PRIMARY KEY (Realm, MatchId)
);
IF OBJECT_ID(N'dbo.PortfolioMatchStats', N'U') IS NULL
CREATE TABLE dbo.PortfolioMatchStats (
    Realm nvarchar(64) NOT NULL,
    MatchId char(32) NOT NULL,
    PlayerId char(32) NOT NULL,
    AccountId nvarchar(32) NOT NULL,
    GameServer nvarchar(32) NOT NULL,
    Kills bigint NOT NULL,
    Deaths bigint NOT NULL,
    Entries bigint NOT NULL,
    CONSTRAINT PK_PortfolioMatchStats PRIMARY KEY (Realm, MatchId, PlayerId),
    CONSTRAINT FK_PortfolioMatchStats_Match FOREIGN KEY (Realm, MatchId) REFERENCES dbo.PortfolioMatch(Realm, MatchId),
    CONSTRAINT CK_PortfolioMatchStats_Counts CHECK (Kills >= 0 AND Deaths >= 0 AND Entries >= 1)
);
GO
CREATE OR ALTER PROCEDURE dbo.PortfolioSaveMatch @Payload xml
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @Realm nvarchar(64) = @Payload.value('(/match/@realm)[1]', 'nvarchar(64)'),
        @MatchId varchar(32) = @Payload.value('(/match/@id)[1]', 'varchar(32)'),
        @Winner int = @Payload.value('(/match/@winner)[1]', 'int'),
        @Complete bit = @Payload.value('(/match/@complete)[1]', 'bit'),
        @Reason varchar(32) = @Payload.value('(/match/@reason)[1]', 'varchar(32)');
    IF LEN(@Realm) = 0 OR LEN(@MatchId) <> 32 OR @Winner NOT BETWEEN 0 AND 3 OR @Reason NOT IN ('round_finished', 'server_shutdown')
        THROW 51000, 'Invalid match result', 1;
    BEGIN TRANSACTION;
    -- 같은 경기의 종료 저장 재시도는 최초 확정된 결과를 그대로 둔다.
    -- 헤더/전원 결과가 한 트랜잭션이므로 헤더만 저장된 완료 결과는 생기지 않는다.
    IF NOT EXISTS (SELECT 1 FROM dbo.PortfolioMatch WITH (UPDLOCK, HOLDLOCK) WHERE Realm=@Realm AND MatchId=@MatchId)
    BEGIN
        INSERT dbo.PortfolioMatch(Realm,MatchId,Winner,Complete,EndReason) VALUES(@Realm,@MatchId,@Winner,@Complete,@Reason);
        INSERT dbo.PortfolioMatchStats(Realm,MatchId,PlayerId,AccountId,GameServer,Kills,Deaths,Entries)
        SELECT @Realm,@MatchId,
            p.value('@id','char(32)'),p.value('@name','nvarchar(32)'),p.value('@server','nvarchar(32)'),
            p.value('@kills','bigint'),p.value('@deaths','bigint'),p.value('@entries','bigint')
        FROM @Payload.nodes('/match/player') AS players(p);
    END;
    COMMIT;
    SELECT N'OK',N'1',N'',N'0',N'0';
END;
