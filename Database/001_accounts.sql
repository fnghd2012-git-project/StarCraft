-- 개인 project_db 전용. 기존 테이블을 지우지 않는 최초 스키마/프로시저 설치 스크립트.
IF OBJECT_ID(N'dbo.PortfolioAccount', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.PortfolioAccount (
        Realm varchar(64) NOT NULL,
        AccountId nvarchar(32) COLLATE Latin1_General_100_BIN2 NOT NULL,
        PlayerId char(32) NOT NULL,
        ReentryAt bigint NOT NULL DEFAULT 0,
        CONSTRAINT PK_PortfolioAccount PRIMARY KEY (Realm, AccountId),
        CONSTRAINT UQ_PortfolioPlayer UNIQUE (Realm, PlayerId)
    );
    CREATE TABLE dbo.PortfolioUnit (
        Realm varchar(64) NOT NULL,
        AccountId nvarchar(32) COLLATE Latin1_General_100_BIN2 NOT NULL,
        ServerId varchar(16) NOT NULL,
        UnitIndex int NOT NULL,
        Race varchar(16) NOT NULL,
        CONSTRAINT PK_PortfolioUnit PRIMARY KEY (Realm, AccountId, ServerId),
        CONSTRAINT FK_PortfolioUnit FOREIGN KEY (Realm, AccountId) REFERENCES dbo.PortfolioAccount(Realm, AccountId)
    );
    CREATE TABLE dbo.PortfolioSession (
        Realm varchar(64) NOT NULL,
        AccountId nvarchar(32) COLLATE Latin1_General_100_BIN2 NOT NULL,
        Token char(32) NOT NULL,
        Owner varchar(16) NOT NULL,
        Location varchar(16) NOT NULL,
        LeaseUntil bigint NOT NULL,
        Nonce char(32) NOT NULL DEFAULT '',
        Destination varchar(16) NOT NULL DEFAULT '',
        CONSTRAINT PK_PortfolioSession PRIMARY KEY (Realm, AccountId),
        CONSTRAINT FK_PortfolioSession FOREIGN KEY (Realm, AccountId) REFERENCES dbo.PortfolioAccount(Realm, AccountId)
    );
END;
GO
IF COL_LENGTH('dbo.PortfolioAccount','PasswordSalt') IS NULL
BEGIN
    ALTER TABLE dbo.PortfolioAccount ADD PasswordSalt varchar(32) NOT NULL CONSTRAINT DF_PortfolioSalt DEFAULT '',
        PasswordHash varchar(64) NOT NULL CONSTRAINT DF_PortfolioHash DEFAULT '';
END;
GO
CREATE OR ALTER PROCEDURE dbo.PortfolioSessionCommand
    @Action varchar(16), @Realm varchar(64), @AccountId nvarchar(32),
    @PlayerId char(32), @Server varchar(16), @GameServer varchar(16), @Token char(32),
    @UnitIndex int, @Race varchar(16), @Nonce char(32), @Destination varchar(16),
    @ReentryAt bigint, @Cooldown int, @LeaseSeconds int, @Salt varchar(32), @Hash varchar(64)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    IF @Action='Auth'
    BEGIN
        SELECT ISNULL((SELECT PasswordSalt FROM dbo.PortfolioAccount WHERE Realm=@Realm AND AccountId=@AccountId),''), '', '', '0', '0';
        RETURN;
    END;
    DECLARE @Now bigint = DATEDIFF_BIG(second, '19700101', SYSUTCDATETIME());
    DECLARE @SavedId char(32), @SavedToken char(32), @Owner varchar(16), @Location varchar(16),
        @Until bigint, @SavedNonce char(32), @Target varchar(16), @SavedUnit int, @SavedRace varchar(16), @Blocked bigint;
    BEGIN TRY
        BEGIN TRANSACTION;
        -- 한 계정의 로그인/이동/갱신을 짧은 트랜잭션으로 직렬화한다. 게임 전체 잠금은 없다.
        DECLARE @LockResult int, @Resource nvarchar(255) = N'portfolio:' + @Realm + N':' + @AccountId;
        EXEC @LockResult = sys.sp_getapplock @Resource=@Resource, @LockMode='Exclusive', @LockOwner='Transaction', @LockTimeout=1000;
        IF @LockResult < 0 THROW 51000, 'Account is busy; retry login', 1;
        SELECT @SavedId=PlayerId, @Blocked=ReentryAt FROM dbo.PortfolioAccount WHERE Realm=@Realm AND AccountId=@AccountId;
        SELECT @SavedToken=Token, @Owner=Owner, @Location=Location, @Until=LeaseUntil, @SavedNonce=Nonce, @Target=Destination
            FROM dbo.PortfolioSession WHERE Realm=@Realm AND AccountId=@AccountId;

        IF @Action='Login'
        BEGIN
            IF LEN(@Salt)<>32 OR LEN(@Hash)<>64 THROW 51009, 'Invalid credentials', 1;
            IF @SavedId IS NOT NULL AND NOT EXISTS(SELECT 1 FROM dbo.PortfolioAccount
                WHERE Realm=@Realm AND AccountId=@AccountId AND PasswordSalt COLLATE Latin1_General_100_BIN2=@Salt
                AND PasswordHash COLLATE Latin1_General_100_BIN2=@Hash)
                THROW 51009, 'Invalid account password', 1;
            IF @Until>@Now AND @Location<>'Offline' AND (@SavedToken<>@Token OR @Owner<>@Server OR @Location<>'Game')
                THROW 51001, 'Account already logged in or transfer pending; retry after session lease expires', 1;
            IF @SavedId IS NULL
            BEGIN
                SET @SavedId=LOWER(REPLACE(CONVERT(char(36), NEWID()), '-', ''));
                SET @Blocked=0;
                INSERT dbo.PortfolioAccount(Realm,AccountId,PlayerId,ReentryAt,PasswordSalt,PasswordHash) VALUES(@Realm,@AccountId,@SavedId,0,@Salt,@Hash);
            END;
            SELECT @SavedUnit=UnitIndex, @SavedRace=Race FROM dbo.PortfolioUnit WHERE Realm=@Realm AND AccountId=@AccountId AND ServerId=@GameServer;
            IF @SavedUnit IS NULL
            BEGIN
                SET @SavedUnit=@UnitIndex; SET @SavedRace=@Race;
                INSERT dbo.PortfolioUnit VALUES(@Realm,@AccountId,@GameServer,@UnitIndex,@Race);
            END;
            IF @SavedRace<>@Race THROW 51002, 'Saved unit race differs from server CSV; migrate account data first', 1;
            SET @Until=@Now+@LeaseSeconds;
            IF @SavedToken IS NULL INSERT dbo.PortfolioSession VALUES(@Realm,@AccountId,@Token,@Server,'Game',@Until,'','');
            ELSE UPDATE dbo.PortfolioSession SET Token=@Token,Owner=@Server,Location='Game',LeaseUntil=@Until,Nonce='',Destination=''
                WHERE Realm=@Realm AND AccountId=@AccountId;
        END
        ELSE
        BEGIN
            IF @SavedId IS NULL OR @SavedId<>@PlayerId THROW 51003, 'Account identity mismatch', 1;
            SELECT @SavedUnit=UnitIndex, @SavedRace=Race FROM dbo.PortfolioUnit WHERE Realm=@Realm AND AccountId=@AccountId AND ServerId=@GameServer;
            IF @SavedUnit IS NULL OR @SavedUnit<>@UnitIndex THROW 51004, 'Saved unit mismatch', 1;
            IF @Action='Accept'
            BEGIN
                -- DB 커밋 뒤 응답만 유실된 재시도는 같은 Token/Nonce일 때만 성공한다.
                IF NOT (@Location IN ('Game','World') AND @SavedToken=@Token AND @SavedNonce=@Nonce AND @Owner=@Server AND @Until>@Now)
                BEGIN
                    IF @Location<>'Transfer' OR @Target<>@Server OR @SavedNonce<>@Nonce OR @Until<=@Now
                        THROW 51005, 'Transfer is expired, used, or not committed', 1;
                    SET @Location=CASE WHEN @Server='World' THEN 'World' ELSE 'Game' END;
                    UPDATE dbo.PortfolioSession SET Token=@Token,Owner=@Server,Location=@Location,Destination='',LeaseUntil=@Now+@LeaseSeconds
                        WHERE Realm=@Realm AND AccountId=@AccountId;
                END;
                SET @Until=@Now+@LeaseSeconds;
                UPDATE dbo.PortfolioSession SET LeaseUntil=@Until WHERE Realm=@Realm AND AccountId=@AccountId;
                IF @Server='World' UPDATE dbo.PortfolioAccount SET ReentryAt=@Until+@Cooldown WHERE Realm=@Realm AND AccountId=@AccountId;
            END
            ELSE IF @Action='Transfer'
            BEGIN
                IF NOT (@Location='Transfer' AND @SavedToken=@Token AND @SavedNonce=@Nonce AND @Target=@Destination AND @Until>@Now)
                BEGIN
                    IF @SavedToken<>@Token OR @Owner<>@Server OR @Location NOT IN ('Game','World') OR @Until<=@Now
                        THROW 51006, 'Session ownership lost', 1;
                    IF @Destination='World' AND @Blocked>@Now THROW 51007, 'Reentry cooldown is active', 1;
                    SET @Until=@Now+30;
                    UPDATE dbo.PortfolioSession SET Location='Transfer',LeaseUntil=@Until,Nonce=@Nonce,Destination=@Destination
                        WHERE Realm=@Realm AND AccountId=@AccountId;
                    UPDATE dbo.PortfolioAccount SET ReentryAt=@ReentryAt WHERE Realm=@Realm AND AccountId=@AccountId;
                END;
            END
            ELSE IF @Action='Renew'
            BEGIN
                IF @SavedToken<>@Token OR @Owner<>@Server OR @Location NOT IN ('Game','World') OR @Until<=@Now
                    THROW 51006, 'Session ownership lost', 1;
                SET @Until=@Now+@LeaseSeconds;
                UPDATE dbo.PortfolioSession SET LeaseUntil=@Until WHERE Realm=@Realm AND AccountId=@AccountId;
                -- World 프로세스가 사라져도 마지막 유효 시각부터 대기 시간이 남도록 미리 기록한다.
                IF @Location='World' UPDATE dbo.PortfolioAccount SET ReentryAt=@Until+@Cooldown WHERE Realm=@Realm AND AccountId=@AccountId;
            END
            ELSE IF @Action='Logout'
            BEGIN
                -- 이전 연결의 늦은 로그아웃이 새 연결을 끊지 않도록 Token을 비교한다.
                IF @SavedToken=@Token AND @Owner=@Server AND @Location IN ('Game','World')
                BEGIN
                    IF @Location='World' UPDATE dbo.PortfolioAccount SET ReentryAt=@Now+@Cooldown WHERE Realm=@Realm AND AccountId=@AccountId;
                    UPDATE dbo.PortfolioSession SET Location='Offline',LeaseUntil=0 WHERE Realm=@Realm AND AccountId=@AccountId;
                END;
                SET @Until=0;
            END
            ELSE THROW 51008, 'Unknown database action', 1;
        END;
        SELECT @Blocked=ReentryAt FROM dbo.PortfolioAccount WHERE Realm=@Realm AND AccountId=@AccountId;
        COMMIT;
        SELECT @SavedId, CONVERT(varchar(16),@SavedUnit), @SavedRace, CONVERT(varchar(24),@Blocked), CONVERT(varchar(24),@Until);
    END TRY
    BEGIN CATCH
        IF @@TRANCOUNT>0 ROLLBACK;
        THROW;
    END CATCH;
END;
