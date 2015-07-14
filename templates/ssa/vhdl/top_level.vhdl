--
-- Copyright (C) 2009-2014 Chris McClelland
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
--
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library altera_mf;
use altera_mf.altera_mf_components.all;

entity top_level is
	generic (
		NUM_DEVS       : integer := 1
	);
	port(
		sysClk_in      : in    std_logic;  -- 50MHz system clock
		
		-- USB interface -----------------------------------------------------------------------------
		serClk_in      : in    std_logic;  -- serial clock (async to sysClk_in)
		serData_in     : in    std_logic;  -- serial data in
		serData_out    : out   std_logic   -- serial data out
	);
end entity;

architecture structural of top_level is
	-- Channel read/write interface -----------------------------------------------------------------
	signal chanAddr   : std_logic_vector(6 downto 0);  -- the selected channel (0-127)

	-- Host >> FPGA pipe:
	signal h2fData    : std_logic_vector(7 downto 0);  -- data lines used when the host writes to a channel
	signal h2fValid   : std_logic;                     -- '1' means "on the next clock rising edge, please accept the data on h2fData"
	signal h2fReady   : std_logic;                     -- channel logic can drive this low to say "I'm not ready for more data yet"

	-- Host << FPGA pipe:
	signal f2hData    : std_logic_vector(7 downto 0);  -- data lines used when the host reads from a channel
	signal f2hValid   : std_logic;                     -- channel logic can drive this low to say "I don't have data ready for you"
	signal f2hReady   : std_logic;                     -- '1' means "on the next clock rising edge, put your next byte of data on f2hData"
	-- ----------------------------------------------------------------------------------------------

	-- SPI signals
	signal spiCS      : std_logic_vector(NUM_DEVS-1 downto 0);
	signal spiClk     : std_logic;
	signal spiMOSI    : std_logic;
	signal spiMISO    : std_logic;

	-- Component from the Altera library to give application access to the config flash.
	component altserial_flash_loader
		generic (
			enable_quad_spi_support : natural;
			enable_shared_access    : string;
			enhanced_mode           : natural;
			intended_device_family  : string;
			lpm_type                : string
		);
		port (
			data0out                : out std_logic;
			noe                     : in  std_logic;
			scein                   : in  std_logic;
			asmi_access_granted     : in  std_logic;
			asmi_access_request     : out std_logic;
			dclkin                  : in  std_logic;
			sdoin                   : in  std_logic 
		);
	end component;
begin
	-- CommFPGA module
	comm_fpga_ss : entity work.comm_fpga_ss
		port map(
			clk_in       => sysClk_in,
			reset_in     => '0',
			
			-- USB interface
			serClk_in    => serClk_in,
			serData_in   => serData_in,
			serData_out  => serData_out,

			-- DVR interface -> Connects to application module
			chanAddr_out => chanAddr,
			h2fData_out  => h2fData,
			h2fValid_out => h2fValid,
			h2fReady_in  => h2fReady,
			f2hData_in   => f2hData,
			f2hValid_in  => f2hValid,
			f2hReady_out => f2hReady
		);

	-- Switches & LEDs application
	spi_talk_app : entity work.spi_talk
		generic map (
			NUM_DEVS     => NUM_DEVS
		)
		port map(
			clk_in       => sysClk_in,
			
			-- DVR interface -> Connects to comm_fpga module
			chanAddr_in  => chanAddr,
			h2fData_in   => h2fData,
			h2fValid_in  => h2fValid,
			h2fReady_out => h2fReady,
			f2hData_out  => f2hData,
			f2hValid_out => f2hValid,
			f2hReady_in  => f2hReady,
			
			-- Peripheral interface
			spiClk_out   => spiClk,
			spiData_out  => spiMOSI,
			spiData_in   => spiMISO,
			spiCS_out    => spiCS
		);

	-- Allow application access to config flash
	spi_access : altserial_flash_loader
		generic map (
			enable_quad_spi_support => 0,
			enable_shared_access    => "ON",
			enhanced_mode           => 1,
			intended_device_family  => "Cyclone II",
			lpm_type                => "altserial_flash_loader"
		)
		port map (
			asmi_access_granted => '0',
			asmi_access_request => open,
			noe                 => '0',
			scein               => spiCS(0),
			dclkin              => spiClk,
			sdoin               => spiMOSI,
			data0out            => spiMISO
		);
end architecture;
